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

#if defined(_MSC_VER)
#define ALIGNED_(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALIGNED_(x) __attribute__ ((aligned(x)))
#endif
#endif

#define _ALIGNED_TYPE(t,x) typedef t ALIGNED_(x)

typedef struct ALIGNED_(8) marshaled_j2k_image
{
    unsigned char* data;
    int length;

    int width;
    int height;
    int components;
} MarshaledJ2KImage;

// need to be 100% sure these are exported in the shared lib, so unique macro for dllexport
#ifdef WIN32
#  define CS_DLLEXPORT __declspec(dllexport)
#else
#  define CS_DLLEXPORT
#endif

CS_DLLEXPORT bool CS_allocJ2KImage(MarshaledJ2KImage* img, int length);
CS_DLLEXPORT void CS_freeJ2KImage(MarshaledJ2KImage* img);

CS_DLLEXPORT bool CS_encodeJ2KImage(MarshaledJ2KImage* source, bool lossless, unsigned char* dest, int* dest_len);
CS_DLLEXPORT bool CS_decodeJ2KImage(unsigned char* source, int source_len, MarshaledJ2KImage* dest);

// from opj_intmath.h
static INLINE OPJ_INT32 cs_ceildivpow2(OPJ_INT32 a, OPJ_INT32 b)
{
    return (OPJ_INT32)((a + ((OPJ_INT64)1 << b) - 1) >> b);
}

// error callbacks //
static void error_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Error: %s", msg);
    fflush((FILE*)client_data);
}

static void warning_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Warning: %s", msg);
    fflush((FILE*)client_data);
}

static void info_callback(const char* msg, void* client_data)
{
    fprintf((FILE*)client_data, "CSharpOpenJpeg Info: %s", msg);
    fflush((FILE*)client_data);
}

// Super magic excellent data stuffing struct //
typedef struct opj_buffer_info
{
    unsigned char* data;
    OPJ_SIZE_T size;
    OPJ_OFF_T pos;
} opj_buffer_info_t;

static OPJ_SIZE_T read_callback(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
    opj_buffer_info_t* buffer = (opj_buffer_info_t*)p_user_data;
    OPJ_SIZE_T len = buffer->size - buffer->pos;
    if (len < 0) { len = 0; }
    if (len == 0) { return (OPJ_SIZE_T)-1; }  /* End of file! */
    if ((OPJ_SIZE_T)len > p_nb_bytes) { len = p_nb_bytes; }
    memcpy(p_buffer, buffer->data + buffer->pos, len);
    buffer->pos += len;
    return len;
}

static OPJ_SIZE_T write_callback(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
    opj_buffer_info_t* buffer = (opj_buffer_info_t*)p_user_data;
    OPJ_SIZE_T nb_bytes_write = p_nb_bytes;
    if ((OPJ_SIZE_T)buffer->pos >= buffer->size) { return (OPJ_SIZE_T)-1; }
    if (p_nb_bytes > buffer->size - buffer->pos) {
        nb_bytes_write = buffer->size - buffer->pos;
    }
    memcpy(&buffer->data[buffer->pos], p_buffer, nb_bytes_write);
    buffer->pos += nb_bytes_write;
    return nb_bytes_write;
}

static OPJ_OFF_T skip_callback(OPJ_OFF_T skip, void* p_user_data)
{
    opj_buffer_info_t* buffer = (opj_buffer_info_t*)p_user_data;
    buffer->pos += skip > (OPJ_OFF_T)(buffer->size - buffer->pos) 
        ? buffer->size - buffer->pos : skip;
    // Always return input value to avoid "Problem with skipping JPEG2000 box, stream error"
    return skip;
}

static OPJ_BOOL seek_callback(OPJ_OFF_T seek_pos, void* p_user_data)
{
    opj_buffer_info_t* buffer = (opj_buffer_info_t*)p_user_data;
    if (seek_pos > (OPJ_OFF_T)buffer->size) { return OPJ_FALSE; }
    buffer->pos = seek_pos;
    return OPJ_TRUE;
}

static opj_stream_t* create_buffer_stream(opj_buffer_info_t* p_buffer, OPJ_BOOL input)
{
    if (!p_buffer) { return NULL; }

    opj_stream_t* p_stream = opj_stream_default_create(input);
    if (!p_stream) { return NULL; }

    opj_stream_set_user_data(p_stream, p_buffer, NULL);
    opj_stream_set_user_data_length(p_stream, p_buffer->size);

    if (input) {
        opj_stream_set_read_function(p_stream, read_callback);
    } else {
        opj_stream_set_write_function(p_stream, write_callback);
    }
    opj_stream_set_skip_function(p_stream, skip_callback);
    opj_stream_set_seek_function(p_stream, seek_callback);

    return p_stream;
}

// Public API Meat //
CS_DLLEXPORT bool CS_allocJ2KImage(MarshaledJ2KImage* img, int length)
{
    CS_freeJ2KImage(img);

    img->length = length;
    img->data = calloc(length, sizeof(unsigned char));
    return (img->data);
}

CS_DLLEXPORT void CS_freeJ2KImage(MarshaledJ2KImage* img)
{
    if (!img) { return; }

    if (img->data) { free(img->data); }
    img->length = 0;

    img->width = 0;
    img->height = 0;
    img->components = 0;
}

CS_DLLEXPORT bool CS_encodeJ2KImage(MarshaledJ2KImage* source, bool lossless, unsigned char* dest, int* dest_len)
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
        cparameters.tcp_rates[0] = 0.f;
    }
    else
    {
        cparameters.tcp_numlayers = 5;
        cparameters.tcp_rates[0] = 1920.f;
        cparameters.tcp_rates[1] = 480.f;
        cparameters.tcp_rates[2] = 120.f;
        cparameters.tcp_rates[3] = 30.f;
        cparameters.tcp_rates[4] = 10.f;
        cparameters.irreversible = 1;
        if (source->components >= 3)
        {
            cparameters.tcp_mct = 1;
        }
    }

    cparameters.cp_comment = ""; // HACK

    opj_image_cmptparm_t cmpparm[5];
    memset(&cmpparm[0], 0, 5 * sizeof(opj_image_cmptparm_t));

    for (int i = 0; i < source->components; ++i)
    {
        cmpparm[i].prec = 8;
        cmpparm[i].bpp = 8; 
        cmpparm[i].sgnd = 0;
        cmpparm[i].dx = cparameters.subsampling_dx;
        cmpparm[i].dy = cparameters.subsampling_dy;
        cmpparm[i].w = source->width;
        cmpparm[i].h = source->height;
    }

    // create an opj_image and populate
    enc_img = opj_image_create(source->components, &cmpparm[0], color_space);
    if (enc_img == NULL) { goto cleanup; }

    enc_img->x0 = 0;
    enc_img->y0 = 0;
    enc_img->x1 = source->width;
    enc_img->y1 = source->height;
    int imgsize = source->width * source->height;

    for (int i = 0; i < source->components; ++i) {
        memcpy(enc_img->comps[i].data, source->data + i * imgsize, imgsize);
    }
    
    // get an encoder handle and setup event handling local context
    codec = opj_create_compress(OPJ_CODEC_J2K);
    opj_set_error_handler(codec, error_callback, stderr);
    opj_set_warning_handler(codec, warning_callback, stderr);
    //opj_set_info_handler(codec, info_callback, stderr);

    // setup the encoder parameters using the current image and user params
    opj_setup_encoder(codec, &cparameters, enc_img);

    // open and initialize a bytestream
    opj_buffer_info_t buffer;
    buffer.pos = 0;
    buffer.data = source->data;
    buffer.size = source->length;
    stream = create_buffer_stream(&buffer, OPJ_STREAM_WRITE);
    
    if (!stream) { goto cleanup; }

    // encode the image
    OPJ_BOOL res = opj_start_compress(codec, enc_img, stream);
    res = res && opj_encode(codec, stream);
    res = res && opj_end_compress(codec, stream);
    if (!res) { goto cleanup; }

    dest = calloc(buffer.size, sizeof(unsigned char));
    if (!dest) { goto cleanup; }

    *dest_len = (int)buffer.size;

    memcpy(&dest[0], buffer.data, buffer.size);

    retval = true;

cleanup:
    opj_stream_destroy(stream);
    opj_image_destroy(enc_img);
    opj_destroy_codec(codec);
    
    return retval;
}

CS_DLLEXPORT bool CS_decodeJ2KImage(unsigned char* source, int source_len, MarshaledJ2KImage* dest)
{
    bool retval = false;
    opj_dparameters_t dparameters;
    opj_codec_t* codec;
    opj_stream_t* stream;
    opj_image_t* image = NULL;
    opj_codestream_info_v2_t* cs_info = NULL;

    // initialize default decode params
    opj_set_default_decoder_parameters(&dparameters);

    // get a decoder handle and setup event handling local context
    codec = opj_create_decompress(OPJ_CODEC_J2K);
    opj_set_error_handler(codec, error_callback, stderr);
    opj_set_warning_handler(codec, warning_callback, stderr);
    //opj_set_info_handler(codec, info_callback, stderr);

    // setup decoder
    opj_setup_decoder(codec, &dparameters);

    // open and initialize a bytestream
    opj_buffer_info_t buffer;
    buffer.pos = 0;
    buffer.data = source;
    buffer.size = source_len;

    stream = create_buffer_stream(&buffer, OPJ_STREAM_READ);
    if (!stream) { goto cleanup; }

    if (!(opj_read_header(stream, codec, &image) // read in header
        && opj_set_decode_area(codec, image, dparameters.DA_x0, // set full image decode
            dparameters.DA_y0, dparameters.DA_x1, dparameters.DA_y1)))
    {
        goto cleanup;
    }
    if (!(opj_decode(codec, stream, image) // decode
        && opj_end_decompress(codec, stream))) //finalize
    {
        goto cleanup; 
    }
    // dec_image returns NULL if decode failed.
    if (image == NULL) { goto cleanup; }

    // if we've got no components, something went wrong
    if (&image->comps[0] == NULL) { goto cleanup; }

    // initialize decoded image struct, assume all comps have same width, height, and factor
    const OPJ_UINT32 components = image->numcomps;
    const OPJ_UINT32 factor = image->comps[0].factor;
    const OPJ_UINT32 width = cs_ceildivpow2(image->x1 - image->x0, factor);
    const OPJ_UINT32 height = cs_ceildivpow2(image->y1 - image->y0, factor);

    //reinitialize dest just in case
    CS_freeJ2KImage(dest);

    dest->components = components;
    dest->width = width;
    dest->height = height;
    dest->length = (int)(components * width * height);

    dest->data = calloc((size_t)dest->length, sizeof(OPJ_INT32));
    if (!dest->data) { goto cleanup; }

    for (OPJ_UINT32 comp = 0, chan = 0; comp < components; ++comp, ++chan)
    {
        if (!image->comps[comp].data) { goto cleanup; }

        OPJ_UINT32 offset = chan;
        for (OPJ_UINT32 y = 0; y < height; ++y)
        {
            for (OPJ_UINT32 x = 0; x < width; ++x)
            {
                dest->data[offset] = image->comps[comp].data[y + x];
                offset += components;
            }
        }
    }
    retval = true;

cleanup:
    // cleanup allocations
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    opj_image_destroy(image);

    return retval;
}
