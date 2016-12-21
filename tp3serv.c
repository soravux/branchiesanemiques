// Inspired by:
// https://github.com/cesanta/mongoose/tree/master/examples/multithreaded_restful_server
// http://www.andrewewhite.net/wordpress/2010/04/07/simple-cc-jpeg-writer-part-2-write-to-buffer-in-memory/
// https://github.com/lvsn/gel3014/blob/master/BaseStation/main.py

#include <stddef.h>
#include <stdio.h>
#include "mongoose.h"
#include <jpeglib.h>

static const char *s_http_port = "8000";

int im_width = 800;
int im_height = 600;

void init_buffer(struct jpeg_compress_struct* cinfo) {}

boolean empty_buffer(struct jpeg_compress_struct* cinfo) {
    return TRUE;
}

void term_buffer(struct jpeg_compress_struct* cinfo) {}

static void ev_handler(struct mg_connection *c, int ev, void *p) {
    if (ev == MG_EV_HTTP_REQUEST) {
        struct http_message *hm = (struct http_message *) p;
        if (hm->uri.len == 1) {
            // Showing index.html
            char reply[100] = "<html><body><img src='./stream.mjpg'></body></html>\0";
            mg_printf(c,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %d\r\n"
                      "\r\n"
                      "%s",
                      (int) strlen(reply), reply);
            return;
        }
        printf("Creating MJPG...\n");

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr       jerr;
        struct jpeg_destination_mgr dmgr;
        printf("1\n");

        JOCTET *out_buffer = malloc(im_width * im_height * 3);

        dmgr.init_destination    = init_buffer;
        dmgr.empty_output_buffer = empty_buffer;
        dmgr.term_destination    = term_buffer;
        dmgr.next_output_byte    = out_buffer;
        dmgr.free_in_buffer      = im_width * im_height *3;
        printf("2\n");

        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        printf("3\n");

        cinfo.dest = &dmgr;

        cinfo.image_width      = im_width;
        cinfo.image_height     = im_height;
        cinfo.input_components = 3;
        cinfo.in_color_space   = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality (&cinfo, 75, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        printf("4\n");

        JSAMPROW row_pointer;
        JSAMPLE *buffer = calloc(im_width * im_height, 3);

        /* silly bit of code to get the RGB in the correct order */
        /*for (int x = 0; x < im_width; x++) {
            for (int y = 0; y < im_height; y++) {
                Uint8 *p    = (Uint8 *) image->pixels + y * image->pitch + x * image->format->BytesPerPixel;
                swap (p[0], p[2]);
            }
        }*/

        /* main code to write jpeg data */
        while (cinfo.next_scanline < cinfo.image_height) {      
            row_pointer = (JSAMPROW) &buffer[cinfo.next_scanline * im_width*3];
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        }
        printf("5\n");
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);


        /* Send the reply */
        //snprintf(reply, sizeof(reply), "{ \"uri\": \"%.*s\" }\n", (int) hm->uri.len,
        //        hm->uri.p);
        int data_len = (cinfo.dest->next_output_byte - out_buffer);
        printf("data len: %i\n", data_len);
        char *buf = calloc(data_len + 256, 1);
        printf("buf: %p\n", buf);
        int headersize = snprintf(buf, 256,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %d\r\n"
                "\r\n"
                "--jpgboundary",
                data_len);
        printf("headersize: %i\n", headersize);
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
