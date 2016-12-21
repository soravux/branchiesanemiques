// Copyright (c) 2015 Cesanta Software Limited
// All rights reserved

#include "mongoose.h"
#include "jpeglib.h"

static const char *s_http_port = "8000";

static void ev_handler(struct mg_connection *c, int ev, void *p) {
    if (ev == MG_EV_HTTP_REQUEST) {
        struct http_message *hm = (struct http_message *) p;
        char reply[100];

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr       jerr;
        struct jpeg_destination_mgr dmgr;

        JOCTET * out_buffer   = new JOCTET[image->w * image->h *3];


        dmgr.init_destination    = init_buffer;
        dmgr.empty_output_buffer = empty_buffer;
        dmgr.term_destination    = term_buffer;
        dmgr.next_output_byte    = out_buffer;
        dmgr.free_in_buffer      = image->w * image->h *3;

        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        cinfo.dest = &dmgr;

        cinfo.image_width      = image->w;
        cinfo.image_height     = image->h;
        cinfo.input_components = 3;
        cinfo.in_color_space   = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality (&cinfo, 75, true);
        jpeg_start_compress(&cinfo, true);

        JSAMPROW row_pointer;
        Uint8 *buffer    = (Uint8*) image->pixels;

        /* silly bit of code to get the RGB in the correct order */
        for (int x = 0; x < image->w; x++) {
            for (int y = 0; y < image->h; y++) {
                Uint8 *p    = (Uint8 *) image->pixels + y * image->pitch + x * image->format->BytesPerPixel;
                swap (p[0], p[2]);
            }
        }

        /* main code to write jpeg data */
        while (cinfo.next_scanline < cinfo.image_height) {      
            row_pointer = (JSAMPROW) &buffer[cinfo.next_scanline * image->pitch];
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        }
        jpeg_finish_compress(&cinfo);


        /* Send the reply */
        //snprintf(reply, sizeof(reply), "{ \"uri\": \"%.*s\" }\n", (int) hm->uri.len,
        //        hm->uri.p);
        int data_len = (cinfo.dest->next_output_byte - out_buffer);
        char buf[data_len + 256];
        int headersize = snprintf(buf,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %d\r\n"
                "\r\n"
                "--jpgboundary",
                data_len);
        memcpy(buf + headersize, out_buffer, data_len);
        mg_send(c, buf, headersize + data_len);
    }
}

int main(void) {
    struct mg_mgr mgr;
    struct mg_connection *nc;

    mg_mgr_init(&mgr, NULL);
    nc = mg_bind(&mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(nc);

    /* For each new connection, execute ev_handler in a separate thread */
    mg_enable_multithreading(nc);

    printf("Starting multi-threaded server on port %s\n", s_http_port);
    for (;;) {
        mg_mgr_poll(&mgr, 3000);
    }
    mg_mgr_free(&mgr);

    return 0;
}
