#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

#include <bcm_host.h>
#include <ilclient.h>

#include "../log/log.h"
#include "jpeg.h"

typedef struct IJPEGEncoder IJPEGEncoder;

struct IJPEGEncoder {
    JPEGEncoder e;

    ILCLIENT_T* client;
    COMPONENT_T* component;
    sem_t semaphore;
};

static void omx_buffer_fill_done(void* data, COMPONENT_T* comp) {
    IJPEGEncoder* ctx = (IJPEGEncoder*) data;
    sem_post(&ctx->semaphore);
}

JPEGEncoder* jpeg_create_encoder() {
    IJPEGEncoder* ctx = calloc(1, sizeof (IJPEGEncoder));
    memset(ctx, 0, sizeof (IJPEGEncoder));

    bcm_host_init();

    ctx->client = ilclient_init();
    if (!ctx->client) {
        LOG_ERROR("Failed to init ilclient");
        return NULL;
    }

    OMX_ERRORTYPE rc = OMX_Init();
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to init OMX, error: %x", rc);
        return NULL;
    }

    ilclient_set_fill_buffer_done_callback(ctx->client, omx_buffer_fill_done, ctx);

    sem_init(&ctx->semaphore, 0, 0);

    return (JPEGEncoder*) ctx;
}

int jpeg_init(JPEGEncoder* encoder) {
    IJPEGEncoder* ctx = (IJPEGEncoder*) encoder;

    OMX_PARAM_PORTDEFINITIONTYPE def;
    OMX_IMAGE_PARAM_PORTFORMATTYPE fmt;
    OMX_IMAGE_PARAM_QFACTORTYPE qfactor;
    OMX_ERRORTYPE rc;

    if (0 != ilclient_create_component(
            ctx->client,
            &ctx->component,
            "image_encode",
            ILCLIENT_DISABLE_ALL_PORTS |
            ILCLIENT_ENABLE_INPUT_BUFFERS |
            ILCLIENT_ENABLE_OUTPUT_BUFFERS)) {
        LOG_ERROR("Failed to create image encode component");
        return -1;
    }

    memset(&def, 0, sizeof (OMX_PARAM_PORTDEFINITIONTYPE));
    def.nSize = sizeof (OMX_PARAM_PORTDEFINITIONTYPE);
    def.nVersion.nVersion = OMX_VERSION;
    def.nPortIndex = 340;

    rc = OMX_GetParameter(ILC_GET_HANDLE(ctx->component), OMX_IndexParamPortDefinition, &def);
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to get OMX port param definition for port 340, error: %x", rc);
        return -1;
    }

    def.format.image.nFrameWidth = ctx->e.width;
    def.format.image.nFrameHeight = ctx->e.height;
    def.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    def.format.image.nSliceHeight = ctx->e.height;
    def.format.image.nStride = ctx->e.width;
    def.format.image.eColorFormat = OMX_COLOR_FormatYUV422PackedPlanar;
    def.nBufferSize = def.format.image.nStride * def.format.image.nSliceHeight;
    def.format.image.bFlagErrorConcealment = OMX_FALSE;

    rc = OMX_SetParameter(ILC_GET_HANDLE(ctx->component), OMX_IndexParamPortDefinition, &def);
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to set OMX port param definition for port 340, error: %x", rc);
        return -1;
    }

    memset(&fmt, 0, sizeof (OMX_IMAGE_PARAM_PORTFORMATTYPE));
    fmt.nSize = sizeof (OMX_IMAGE_PARAM_PORTFORMATTYPE);
    fmt.nVersion.nVersion = OMX_VERSION;
    fmt.nPortIndex = 341;
    fmt.eCompressionFormat = OMX_IMAGE_CodingJPEG;

    rc = OMX_SetParameter(ILC_GET_HANDLE(ctx->component), OMX_IndexParamImagePortFormat, &fmt);
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to set OMX port param format for port 341, error: %x", rc);
        return -1;
    }

    qfactor.nSize = sizeof (OMX_IMAGE_PARAM_QFACTORTYPE);
    qfactor.nVersion.nVersion = OMX_VERSION;
    qfactor.nPortIndex = 341;

    rc = OMX_GetParameter(ILC_GET_HANDLE(ctx->component), OMX_IndexParamQFactor, &qfactor);
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to get OMX port param qfactor for port 341, error: %x", rc);
        return -1;
    }

    qfactor.nQFactor = ctx->e.quality;

    rc = OMX_SetParameter(ILC_GET_HANDLE(ctx->component), OMX_IndexParamQFactor, &qfactor);
    if (rc != OMX_ErrorNone) {
        LOG_ERROR("Failed to set OMX port param qfactor for port 341, error: %x", rc);
        return -1;
    }

    if (0 != ilclient_change_component_state(ctx->component, OMX_StateIdle)) {
        LOG_ERROR("Failed to set idle state for image encode component");
        return -1;
    }

    if (0 != ilclient_enable_port_buffers(ctx->component, 340, NULL, NULL, NULL)) {
        LOG_ERROR("Failed to enable buffers for port 340");
        return -1;
    }

    if (0 != ilclient_enable_port_buffers(ctx->component, 341, NULL, NULL, NULL)) {
        LOG_ERROR("Failed to enable buffers for port 341");
        return -1;
    }

    if (0 != ilclient_change_component_state(ctx->component, OMX_StateExecuting)) {
        LOG_ERROR("Failed to set executing state for image encode component");
        return -1;
    }

    return 0;
}

int jpeg_compress(JPEGEncoder* encoder) {
    IJPEGEncoder* ctx = (IJPEGEncoder*) encoder;
    Buffer* input = ctx->e.input;
    Buffer* output = ctx->e.output;

    static OMX_BUFFERHEADERTYPE* ibuf = NULL;
    static OMX_BUFFERHEADERTYPE* obuf = NULL;

    output->used = 0;

    if (ibuf == NULL) {
        ibuf = ilclient_get_input_buffer(ctx->component, 340, 1);
        if (ibuf == NULL) {
            LOG_ERROR("Getting the input buffer");
            return -1;
        }
    }

    ibuf->nFilledLen = input->used;
    if (ibuf->nFilledLen > ibuf->nAllocLen) {
        ibuf->nFilledLen = ibuf->nAllocLen;
    }
    
    int i;
    uint8_t* tmp = ibuf->pBuffer;
    for (i = 0; i < ibuf->nFilledLen / 2; i++) {
        // Y
        tmp[i] = input->data[i * 2];
    }
    
    tmp += i;
    for (i = 0; i < ibuf->nFilledLen / 4; i++) {
        // U
        tmp[i] = input->data[i * 4 + 1];
    }
    
    tmp += i;
    for (i = 0; i < ibuf->nFilledLen / 4; i++) {
        // V
        tmp[i] = input->data[i * 4 + 3];
    }

    if (OMX_ErrorNone != OMX_EmptyThisBuffer(ILC_GET_HANDLE(ctx->component), ibuf)) {
        LOG_ERROR("Reading the input buffer");
        return -1;
    }

    do {
        if (obuf == NULL) {
            obuf = ilclient_get_output_buffer(ctx->component, 341, 1);
            if (obuf == NULL) {
                LOG_ERROR("Getting the output buffer");
                return -1;
            }
        }

        obuf->nFilledLen = 0;

        if (OMX_ErrorNone != OMX_FillThisBuffer(ILC_GET_HANDLE(ctx->component), obuf)) {
            LOG_ERROR("Writing the output buffer");
            return -1;
        }

        sem_wait(&ctx->semaphore);

        buffer_resize(output, output->used + obuf->nFilledLen, 0);
        memcpy(output->data + output->used, obuf->pBuffer, obuf->nFilledLen);
        output->used += obuf->nFilledLen;

        LOG_TRACE("OBuffer size %d", obuf->nFilledLen);
        LOG_TRACE("OBuffer Flags %d", obuf->nFlags);
    } while (obuf->nFilledLen == 81920);

    obuf->nFilledLen = 0;

    return 0;
}

int jpeg_destroy_encoder(JPEGEncoder* encode) {
    IJPEGEncoder* ctx = (IJPEGEncoder*) encode;

    ilclient_disable_port_buffers(ctx->component, 340, NULL, NULL, NULL);
    ilclient_disable_port_buffers(ctx->component, 341, NULL, NULL, NULL);
    ilclient_cleanup_components(&ctx->component);

    OMX_Deinit();
    ilclient_destroy(ctx->client);
    sem_destroy(&ctx->semaphore);

    free(ctx);
    return 0;
}