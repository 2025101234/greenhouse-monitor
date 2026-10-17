#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gh_network.h"
#include "gh_data.h"
#include "gh_config.h"

rt_size_t gh_network_make_status_json(char *buffer, rt_size_t buffer_size)
{
    gh_status_t status;
    int length;

    if ((buffer == RT_NULL) || (buffer_size == 0))
        return 0;
    if (gh_data_get_status(&status) != RT_EOK)
        return 0;
    length = rt_snprintf(buffer, buffer_size,
        "{\"seq\":%lu,\"temp_x10\":%ld,\"humidity_x10\":%ld,"
        "\"light_lux\":%lu,\"valid\":%u,\"alarm\":%lu,"
        "\"temp_high_x10\":%ld,\"humidity_high_x10\":%ld,"
        "\"light_high_lux\":%lu}",
        (unsigned long)status.sample.sequence,
        (long)status.sample.air_temp_x10,
        (long)status.sample.air_humidity_x10,
        (unsigned long)status.sample.light_lux,
        (unsigned int)status.sample.valid_mask,
        (unsigned long)status.alarm_mask,
        (long)status.thresholds.temp_high_x10,
        (long)status.thresholds.humidity_high_x10,
        (unsigned long)status.thresholds.light_high_lux);
    if (length < 0)
        return 0;
    return (rt_size_t)length >= buffer_size ? buffer_size - 1 : (rt_size_t)length;
}

#if GH_ENABLE_HTTP_SERVER && defined(RT_USING_LWIP) && defined(RT_USING_NETDEV) && defined(RT_USING_SAL)
#include <lwip/opt.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#if defined(AIC_USING_REALTEK_WLAN0)
#include "ethernetif_wlan.h"
#endif

static const char g_http_index[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Greenhouse</title><style>body{font-family:Arial;background:#0b1220;color:#fff;"
    "max-width:620px;margin:20px auto;padding:12px} .card{background:#18233a;padding:14px;"
    "margin:8px 0;border-radius:10px} input{width:90px;margin:4px} button{padding:8px}"
    "</style></head><body><h2>RT-Thread Greenhouse</h2>"
    "<div class='card' id='values'>Loading...</div><div class='card'>"
    "Temperature <input id='t' type='number' step='0.1'> C<br>"
    "Humidity <input id='h' type='number' step='0.1'> %<br>"
    "Light <input id='l' type='number' step='100'> lux<br>"
    "<button onclick='save()'>Save thresholds</button></div>"
    "<script>async function refresh(){let r=await fetch('/api/status');let d=await r.json();"
    "let f=(x)=> (x/10).toFixed(1); document.getElementById('values').innerHTML="
    "'Temperature: '+f(d.temp_x10)+' C<br>Humidity: '+f(d.humidity_x10)+' %<br>Light: '+"
    "d.light_lux+' lux<br>Alarm: 0x'+d.alarm.toString(16);"
    "document.getElementById('t').value=f(d.temp_high_x10);document.getElementById('h').value=f(d.humidity_high_x10);"
    "document.getElementById('l').value=d.light_high_lux;}"
    "async function save(){let tEl=document.getElementById('t'),hEl=document.getElementById('h'),"
    "lEl=document.getElementById('l');let q='?temp_x10='+Math.round(parseFloat(tEl.value)*10)+'&humidity_x10='+"
    "Math.round(parseFloat(hEl.value)*10)+'&light_lux='+parseInt(lEl.value);"
    "let r=await fetch('/api/threshold'+q,{method:'POST'});alert(await r.text());refresh();}"
    "refresh();setInterval(refresh,2000);</script></body></html>";

static int gh_http_send_all(int fd, const char *data, rt_size_t length)
{
    rt_size_t sent = 0;
    int result;

    while (sent < length)
    {
        result = send(fd, data + sent, length - sent, 0);
        if (result <= 0)
            return -1;
        sent += (rt_size_t)result;
    }
    return 0;
}

static void gh_http_reply(int fd, const char *status, const char *type,
                          const char *body, rt_size_t body_len)
{
    char header[192];
    int length = rt_snprintf(header, sizeof(header),
                             "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
                             "Content-Length: %lu\r\nConnection: close\r\n\r\n",
                             status, type, (unsigned long)body_len);
    if ((length > 0) && ((rt_size_t)length < sizeof(header)))
        gh_http_send_all(fd, header, (rt_size_t)length);
    gh_http_send_all(fd, body, body_len);
}

static rt_bool_t gh_http_parse_long(const char *request, const char *key, long *value)
{
    const char *start = strstr(request, key);
    char *end;
    long parsed;

    if (start == RT_NULL)
        return RT_FALSE;
    start += strlen(key);
    errno = 0;
    parsed = strtol(start, &end, 10);
    if ((end == start) || (errno == ERANGE))
        return RT_FALSE;
    /* Reject a valid prefix such as "350oops".  Query parameters must end
     * at '&', whitespace, CR/LF, or the request terminator. */
    if ((*end != '&') && (*end != ' ') && (*end != '\r') &&
        (*end != '\n') && (*end != '\0'))
        return RT_FALSE;
    *value = parsed;
    return RT_TRUE;
}

static void gh_http_handle_client(int fd)
{
    char request[GH_HTTP_RX_BUFFER_SIZE];
    char json[GH_HTTP_TX_BUFFER_SIZE];
    gh_thresholds_t limits;
    long temp;
    long humidity;
    long light;
    int received;

    received = recv(fd, request, sizeof(request) - 1, 0);
    if (received <= 0)
        return;
    request[received] = '\0';

    if (!strncmp(request, "GET /api/status ", 16))
    {
        rt_size_t length = gh_network_make_status_json(json, sizeof(json));
        gh_http_reply(fd, "200 OK", "application/json", json, length);
    }
    else if (!strncmp(request, "POST /api/threshold", 19))
    {
        if (!gh_http_parse_long(request, "temp_x10=", &temp) ||
            !gh_http_parse_long(request, "humidity_x10=", &humidity) ||
            !gh_http_parse_long(request, "light_lux=", &light) ||
            (temp < -400) || (temp > 1250) ||
            (humidity < 0) || (humidity > 1000) ||
            (light < 0) || (light > (long)GH_MAX_LIGHT_LUX) ||
            gh_data_get_thresholds(&limits) != RT_EOK)
        {
            const char *body = "invalid threshold parameters";
            gh_http_reply(fd, "400 Bad Request", "text/plain", body, strlen(body));
        }
        else
        {
            limits.temp_high_x10 = (rt_int32_t)temp;
            limits.humidity_high_x10 = (rt_int32_t)humidity;
            limits.light_high_lux = (rt_uint32_t)light;
            if (gh_data_set_thresholds(&limits) != RT_EOK)
            {
                const char *body = "threshold out of range";
                gh_http_reply(fd, "400 Bad Request", "text/plain", body, strlen(body));
            }
            else
            {
                const char *body = "threshold saved";
                gh_http_reply(fd, "200 OK", "text/plain", body, strlen(body));
            }
        }
    }
    else if (!strncmp(request, "GET / ", 6) || !strncmp(request, "GET /?", 6))
    {
        gh_http_reply(fd, "200 OK", "text/html; charset=utf-8", g_http_index,
                      sizeof(g_http_index) - 1);
    }
    else
    {
        const char *body = "not found";
        gh_http_reply(fd, "404 Not Found", "text/plain", body, strlen(body));
    }
}

static rt_err_t gh_http_prepare_network(void)
{
#if defined(AIC_USING_REALTEK_WLAN0)
    /* The SDK's example application normally calls netif_startup().  This
     * project does not include that example, so initialise the RTL8189
     * netif here before opening the HTTP listener. */
    rt_bool_t wlan_present = RT_FALSE;
    struct netif *netif;

    for (netif = netif_list; netif != RT_NULL; netif = netif->next)
    {
        if ((netif->name[0] == 'W') && (netif->name[1] == 'L'))
        {
            wlan_present = RT_TRUE;
            break;
        }
    }
    if (!wlan_present)
    {
#if LWIP_IPV4
        ip4_addr_t ipaddr;
        ip4_addr_t netmask;
        ip4_addr_t gateway;

        ip4_addr_set_zero(&ipaddr);
        ip4_addr_set_zero(&netmask);
        ip4_addr_set_zero(&gateway);
        if (init_wlan_netif(&ipaddr, &netmask, &gateway) != 0)
            return -RT_ERROR;
#else
        if (init_wlan_netif() != 0)
            return -RT_ERROR;
#endif
    }
#endif
    return RT_EOK;
}

static void gh_http_server_thread(void *parameter)
{
    int server_fd;
    int client_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    RT_UNUSED(parameter);

    if (gh_http_prepare_network() != RT_EOK)
    {
        rt_kprintf("[GH] Wi-Fi netif initialisation failed; HTTP stopped\n");
        return;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0)
    {
        rt_kprintf("[GH] HTTP socket create failed\n");
        return;
    }
    rt_memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(GH_HTTP_SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 ||
        listen(server_fd, 2) < 0)
    {
        rt_kprintf("[GH] HTTP bind/listen failed, errno=%d\n", errno);
        closesocket(server_fd);
        return;
    }
    rt_kprintf("[GH] HTTP server listening on port %d\n", GH_HTTP_SERVER_PORT);
    while (1)
    {
        client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
            continue;
        gh_http_handle_client(client_fd);
        closesocket(client_fd);
    }
}
#endif

static void gh_network_thread(void *parameter)
{
    RT_UNUSED(parameter);
#if GH_ENABLE_HTTP_SERVER && defined(RT_USING_LWIP) && defined(RT_USING_NETDEV) && defined(RT_USING_SAL)
    gh_http_server_thread(RT_NULL);
#else
    rt_kprintf("[GH] HTTP disabled: enable RTL8189, SAL, NETDEV and LwIP\n");
    while (1)
        rt_thread_mdelay(60000);
#endif
}

rt_err_t gh_network_start(void)
{
    rt_thread_t thread = rt_thread_create("ghnet", gh_network_thread, RT_NULL,
                                          6144, 24, 10);
    if (thread == RT_NULL)
        return -RT_ENOMEM;
    return rt_thread_startup(thread);
}
