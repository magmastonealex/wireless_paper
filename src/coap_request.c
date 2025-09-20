#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

#include "coap_request.h"

LOG_MODULE_REGISTER(coap_request, LOG_LEVEL_INF);

struct coap_request_context {
    struct k_sem completion_sem;
    coap_stream_callback_t stream_cb;
    void *user_data;
    coap_request_result_t result;
    size_t current_offset;
    int sockfd;
    bool callback_aborted;
    struct coap_client *client;
};

static void internal_coap_callback(int16_t result_code, size_t offset, const uint8_t *payload,
                                 size_t len, bool last_block, void *user_data)
{
    struct coap_request_context *ctx = (struct coap_request_context *)user_data;

    LOG_DBG("CoAP callback: code=%d, offset=%zu, len=%zu, last=%s",
            result_code, offset, len, last_block ? "true" : "false");

    if (ctx->callback_aborted) {
        LOG_DBG("Callback already aborted, ignoring");
        return;
    }

    if (result_code != COAP_RESPONSE_CODE_CONTENT) {
        LOG_ERR("CoAP protocol error: %d", result_code);
        ctx->result = COAP_REQUEST_PROTO_ERROR;
        k_sem_give(&ctx->completion_sem);
        return;
    }

    if (offset != ctx->current_offset) {
        LOG_ERR("Out-of-order data: expected offset %zu, got %zu",
                ctx->current_offset, offset);
        ctx->result = COAP_REQUEST_PROTO_ERROR;
        k_sem_give(&ctx->completion_sem);
        return;
    }

    if (ctx->stream_cb && len > 0) {
        int cb_result = ctx->stream_cb(payload, len, offset, last_block, ctx->user_data);
        if (cb_result < 0) {
            LOG_WRN("Stream callback requested abort: %d", cb_result);
            ctx->callback_aborted = true;
            ctx->result = COAP_REQUEST_CALLBACK_ABORT;
            coap_client_cancel_requests(ctx->client);
            k_sem_give(&ctx->completion_sem);
            return;
        }
    }

    ctx->current_offset = offset + len;

    if (last_block) {
        LOG_INF("Transfer complete, %zu bytes", ctx->current_offset);
        ctx->result = COAP_REQUEST_SUCCESS;
        k_sem_give(&ctx->completion_sem);
    }
}

coap_request_result_t do_coap_request(struct coap_client *client, struct sockaddr *server_addr,
                                    const char* path, enum coap_method method, const uint8_t* payload,
                                    size_t payload_len, coap_stream_callback_t stream_cb,
                                    void* user_data, uint32_t timeout_seconds)
{
    struct coap_request_context ctx = {0};
    int ret;

    if (!client || !server_addr || !path) {
        return COAP_REQUEST_PROTO_ERROR;
    }

    k_sem_init(&ctx.completion_sem, 0, 1);
    ctx.stream_cb = stream_cb;
    ctx.user_data = user_data;
    ctx.result = COAP_REQUEST_NETWORK_ERROR;
    ctx.current_offset = 0;
    ctx.callback_aborted = false;
    ctx.client = client;

    ctx.sockfd = zsock_socket(server_addr->sa_family, SOCK_DGRAM, 0);
    if (ctx.sockfd < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return COAP_REQUEST_NETWORK_ERROR;
    }

    struct coap_client_request request = {
        .method = method,
        .confirmable = true,
        .path = path,
        .payload = payload,
        .len = payload_len,
        .cb = internal_coap_callback,
        .options = NULL,
        .num_options = 0,
        .user_data = &ctx
    };

    LOG_INF("Starting CoAP %s request to %s",
            method == COAP_METHOD_GET ? "GET" :
            method == COAP_METHOD_POST ? "POST" : "OTHER", path);

    ret = coap_client_req(client, ctx.sockfd, server_addr, &request, NULL);
    if (ret < 0) {
        LOG_ERR("Failed to send CoAP request: %d", ret);
        zsock_close(ctx.sockfd);
        return COAP_REQUEST_NETWORK_ERROR;
    }

    ret = k_sem_take(&ctx.completion_sem, K_SECONDS(timeout_seconds));

    if (ret == -EAGAIN) {
        LOG_WRN("CoAP request timed out after %u seconds", timeout_seconds);
        coap_client_cancel_requests(client);
        ctx.result = COAP_REQUEST_TIMEOUT;
    }

    zsock_close(ctx.sockfd);

    LOG_DBG("CoAP request completed with result: %d", ctx.result);
    return ctx.result;
}