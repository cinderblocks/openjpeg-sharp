/**
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2007, Patrick Piscaglia (Telemis)
 * Copyright (c) 2020, Sjofn, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "openjpeg.h"

struct MarshalledImage
{
    unsigned char* encoded;
    int length;

    unsigned char* decoded;
    int width;
    int height;
    int layers;
    int resolutions;
    int components;
    int packet_count;
    opj_packet_info_t* packet_ptr;
};

// need to be 100% sure these are exported in the shared lib, so unique macro for dllexport
#ifdef WIN32
#  define CS_DLLEXPORT __declspec(dllexport)
#else
#  define CS_DLLEXPORT
#endif

CS_DLLEXPORT bool CS_allocEncoded(struct MarshalledImage* image);
CS_DLLEXPORT bool CS_allocDecoded(struct MarshalledImage* image);
CS_DLLEXPORT void CS_freeImageAlloc(struct MarshalledImage* image);

CS_DLLEXPORT bool CS_encodeImage(struct MarshalledImage* image, bool lossless);
CS_DLLEXPORT bool CS_decodeImage(struct MarshalledImage* image);

// error callbacks //
static void error_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Error: %s", msg);
}

static void warning_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Warning: %s", msg);
}

static void info_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Info: %s", msg);
}

// Super magic excellent data stuffing struct //
struct JPXData
{
    unsigned char* data;
    OPJ_SIZE_T size;
    OPJ_OFF_T pos;
};

static OPJ_SIZE_T jpxRead_callback(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
    struct JPXData* jpxData = (struct JPXData*)p_user_data;
    OPJ_SIZE_T len = jpxData->size - jpxData->pos;
    if (len < 0) { len = 0; }
    if (len == 0) { return (OPJ_SIZE_T)-1; }  /* End of file! */
    if ((OPJ_SIZE_T)len > p_nb_bytes) { len = p_nb_bytes; }
    memcpy(p_buffer, jpxData->data + jpxData->pos, len);
    jpxData->pos += len;
    return len;
}

static OPJ_SIZE_T jpxWrite_callback(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
    struct JPXData* jpxData = (struct JPXData*)p_user_data;
    OPJ_SIZE_T nb_bytes_write = p_nb_bytes;
    if ((OPJ_SIZE_T)jpxData->pos >= jpxData->size) { return (OPJ_SIZE_T)-1; }
    if (p_nb_bytes > jpxData->size - jpxData->pos) {
        nb_bytes_write = jpxData->size - jpxData->pos;
    }
    memcpy(&(jpxData->data[jpxData->pos]), p_buffer, nb_bytes_write);
    jpxData->pos += nb_bytes_write;
    return nb_bytes_write;
}

static OPJ_OFF_T jpxSkip_callback(OPJ_OFF_T skip, void* p_user_data)
{
    struct JPXData* jpxData = (struct JPXData*)p_user_data;
    jpxData->pos += skip > (OPJ_OFF_T)(jpxData->size - jpxData->pos) ? jpxData->size - jpxData->pos : skip;
    // Always return input value to avoid "Problem with skipping JPEG2000 box, stream error"
    return skip;
}

static OPJ_BOOL jpxSeek_callback(OPJ_OFF_T seek_pos, void* p_user_data)
{
    struct JPXData* jpxData = (struct JPXData*)p_user_data;
    if (seek_pos > (OPJ_OFF_T)jpxData->size) { return OPJ_FALSE; }
    jpxData->pos = seek_pos;
    return OPJ_TRUE;
}

// Public API Meat //
CS_DLLEXPORT bool CS_allocEncoded(struct MarshalledImage* image)
{
    CS_freeImageAlloc(image);

    unsigned char* alloc = calloc(image->length, sizeof(unsigned char));
    if (!alloc) { return false; }

    image->encoded = alloc;
    image->decoded = NULL;

    return true;
}

CS_DLLEXPORT bool CS_allocDecoded(struct MarshalledImage* image)
{
    CS_freeImageAlloc(image);

    unsigned char* alloc = calloc(image->length, sizeof(unsigned char));
    if (!alloc) { return false; }

    image->decoded = alloc;
    image->encoded = NULL;

    return true;
}

CS_DLLEXPORT void CS_freeImageAlloc(struct MarshalledImage* image)
{
    if (image->encoded)
    {
        free(image->encoded);
        image->encoded = NULL;
    }
    if (image->decoded)
    {
        free(image->decoded);
        image->decoded = NULL;
    }
}

CS_DLLEXPORT bool CS_encodeImage(struct MarshalledImage* image, bool lossless)
{
    bool retval = false;
    opj_cparameters_t cparameters;
    opj_image_t* enc_img;
    opj_codec_t* codec = NULL;
    opj_stream_t* stream = NULL;
    OPJ_COLOR_SPACE color_space = OPJ_CLRSPC_SRGB;

    // initialize encoder params
    opj_set_default_encoder_parameters(&cparameters);
    cparameters.cp_disto_alloc = 1;

    if (lossless)
    {
        cparameters.tcp_numlayers = 1;
        cparameters.tcp_rates[0] = 0;
    }
    else
    {
        cparameters.tcp_numlayers = 5;
        cparameters.tcp_rates[0] = 1920;
        cparameters.tcp_rates[1] = 480;
        cparameters.tcp_rates[2] = 120;
        cparameters.tcp_rates[3] = 30;
        cparameters.tcp_rates[4] = 10;
        cparameters.irreversible = 1;
        if (image->components >= 3)
        {
            cparameters.tcp_mct = 1;
        }
    }

    cparameters.cp_comment = ""; // HACK

    opj_image_cmptparm_t cmpparm[5];
    memset(&cmpparm[0], 0, 5 * sizeof(opj_image_cmptparm_t));

    for (int i = 0; i < image->components; ++i)
    {
        cmpparm[i].prec = 8;
        cmpparm[i].bpp = 8; 
        cmpparm[i].sgnd = 0;
        cmpparm[i].dx = 1; // *TODO: handle sub-sampling
        cmpparm[i].dy = 1;
        cmpparm[i].w = image->width;
        cmpparm[i].h = image->height;
    }

    // create an opj_image and populate
    enc_img = opj_image_create(image->components, &cmpparm[0], color_space);
    if (enc_img == NULL) { goto cleanup; }

    enc_img->x0 = 0;
    enc_img->y0 = 0;
    enc_img->x1 = image->width;
    enc_img->y1 = image->height;
    int imgsize = image->width * image->height;

    for (int i = 0; i < image->components; ++i) {
        memcpy(enc_img->comps[i].data, image->decoded + i * imgsize, imgsize);
    }
    
    // get an encoder handle and setup event handling local context
    codec = opj_create_compress(OPJ_CODEC_J2K);
    opj_set_error_handler(codec, error_callback, stderr);
    opj_set_warning_handler(codec, warning_callback, stderr);
    opj_set_info_handler(codec, info_callback, stdout);

    // setup the encoder parameters using the current image and user params
    opj_setup_encoder(codec, &cparameters, enc_img);

    // open and initialize a bytestream
    stream = opj_stream_default_create(OPJ_STREAM_WRITE);
    if (!stream) { goto cleanup; }

    struct JPXData jpxdata;
    memset(&jpxdata, 0, sizeof(struct JPXData));
    jpxdata.data = image->decoded;
    jpxdata.size = image->length;

    opj_stream_set_user_data(stream, &jpxdata, NULL);
    opj_stream_set_write_function(stream, jpxWrite_callback);
    opj_stream_set_skip_function(stream, jpxSkip_callback);
    opj_stream_set_seek_function(stream, jpxSeek_callback);

    // encode the image
    OPJ_BOOL res = opj_start_compress(codec, enc_img, stream);
    res = res && opj_encode(codec, stream);
    res = res && opj_end_compress(codec, stream);
    if (!res) { goto cleanup; }

    image->length = (int)jpxdata.size;

    unsigned char* alloc = calloc(image->length, sizeof(unsigned char));
    if (!alloc) { goto cleanup; }

    image->encoded = alloc;
    memcpy(image->encoded, jpxdata.data, image->length);
    free(alloc);

    retval = true;

cleanup:
    opj_stream_destroy(stream);
    opj_image_destroy(enc_img);
    opj_destroy_codec(codec);
    
    return retval;
}

CS_DLLEXPORT bool CS_decodeImage(struct MarshalledImage* image)
{
    bool retval = false;
    opj_dparameters_t dparameters;
    opj_codec_t* codec;
    opj_stream_t* stream;
    opj_image_t* dec_img = NULL;
    opj_codestream_info_v2_t* cs_info = NULL;
    opj_codestream_index_t* cs_index = NULL;

    // initialize default decode params
    opj_set_default_decoder_parameters(&dparameters);

    // get a decoder handle and setup event handling local context
    codec = opj_create_decompress(OPJ_CODEC_J2K);
    opj_set_error_handler(codec, error_callback, stderr);
    opj_set_warning_handler(codec, warning_callback, stderr);
    opj_set_info_handler(codec, info_callback, stdout);

    // setup decoder
    opj_setup_decoder(codec, &dparameters);

    // open and initialize a bytestream
    stream = opj_stream_default_create(OPJ_STREAM_READ);
    if (!stream) { goto cleanup; }

    struct JPXData jpxdata;
    memset(&jpxdata, 0, sizeof(struct JPXData));
    jpxdata.data = image->encoded;
    jpxdata.size = image->length;

    opj_stream_set_user_data(stream, &jpxdata, NULL);
    opj_stream_set_read_function(stream, jpxRead_callback);
    opj_stream_set_skip_function(stream, jpxSkip_callback);
    opj_stream_set_seek_function(stream, jpxSeek_callback);

    if (!opj_read_header(stream, codec, &dec_img)) { goto cleanup; }
    if (!opj_decode(codec, stream, dec_img)) { goto cleanup; }

    // dec_image returns NULL if decode failed.
    if (dec_img == NULL) { goto cleanup; }

    cs_index = opj_get_cstr_index(codec);
    if (!cs_index) { goto cleanup; }

    // copy from image struct
    image->width = dec_img->x1 - dec_img->x0;
    image->height = dec_img->y1 - dec_img->y0;
    image->components = dec_img->numcomps;

    // copy from cs_index struct
    image->packet_count = cs_index->nb_of_tiles;
    image->packet_ptr = cs_index->tile_index->packet_index;

    // copy from cs_info struct
    cs_info = opj_get_cstr_info(codec);
    if (cs_info)
    {
        image->layers = cs_info->m_default_tile_info.numlayers;
        image->resolutions = cs_info->m_default_tile_info.tccp_info->numresolutions;
    }
    
    int imgsize = image->width * image->height;

    unsigned char* alloc = calloc(imgsize * image->components, sizeof(unsigned char));
    if (!alloc) { return false; }

    for (int i = 0; i < image->components; ++i) {
        memcpy(image->decoded + i * imgsize, dec_img->comps[i].data, imgsize);
    }

    retval = true;

cleanup:
    // cleanup allocations
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    opj_image_destroy(dec_img);

    return retval;
}
