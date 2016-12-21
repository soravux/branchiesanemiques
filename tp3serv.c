// Inspired by:
// https://github.com/cesanta/mongoose/tree/master/examples/multithreaded_restful_server
// http://www.andrewewhite.net/wordpress/2010/04/07/simple-cc-jpeg-writer-part-2-write-to-buffer-in-memory/
// https://github.com/lvsn/gel3014/blob/master/BaseStation/main.py
// https://github.com/brad/swftools/blob/master/lib/jpeg.c

#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mongoose.h"
#include <jpeglib.h>

// Networking
static const char *s_http_port = "8000";
#define MAX_CONN 8
struct mg_connection *tc[MAX_CONN] = {0};

void add_conn(struct mg_connection *c) {
    for (int i = 0; i < MAX_CONN; ++i) {
        if (tc[i] == NULL) {
            tc[i] = c;
            break;
        }
    }
}

void remove_conn(struct mg_connection *c) {
    for (int i = 0; i < MAX_CONN; ++i) {
        if (tc[i] == c) {
            tc[i] = NULL;
        }
    }
}


// Image (JPEG) compression
#define OUTBUFFER_SIZE 0x8000
int im_width = 800;
int im_height = 600;


struct image {
	JOCTET *data;
	int len;
};



void init_buffer(struct jpeg_compress_struct *cinfo) {
  struct jpeg_destination_mgr *dmgr = (struct jpeg_destination_mgr*)(cinfo->dest);
  JOCTET *buffer = (JOCTET*)malloc(OUTBUFFER_SIZE);
  if (!buffer) {
      perror("malloc");
      printf("Out of memory!\n");
      exit(1);
  }
  dmgr->next_output_byte = buffer;
  dmgr->free_in_buffer = OUTBUFFER_SIZE;
}

boolean empty_buffer() {
    printf("jpeg mem overflow!\n");
    exit(1);

    return TRUE;
}

void term_buffer(struct jpeg_compress_struct* cinfo) {
	//struct jpeg_destination_mgr *dmgr = (struct jpeg_destination_mgr*)(cinfo->dest);
	//datalen = OUTBUFFER_SIZE - dmgr->free_in_buffer;
	//dmgr->free_in_buffer = 0;
}

struct image encode_jpeg(JSAMPLE *in) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr       jerr;
    struct jpeg_destination_mgr dmgr;
	memset(&cinfo, 0, sizeof(cinfo));
	memset(&jerr, 0, sizeof(jerr));
	memset(&dmgr, 0, sizeof(dmgr));

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    dmgr.init_destination    = init_buffer;
    dmgr.empty_output_buffer = empty_buffer;
    dmgr.term_destination    = term_buffer;

    cinfo.dest = &dmgr;

    cinfo.image_width      = im_width;
    cinfo.image_height     = im_height;
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_RGB;

    jpeg_set_defaults(&cinfo);
	cinfo.dct_method = JDCT_IFAST;
    jpeg_set_quality (&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer;

    /* silly bit of code to get the RGB in the correct order */
    /*for (int x = 0; x < im_width; x++) {
        for (int y = 0; y < im_height; y++) {
            Uint8 *p    = (Uint8 *) image->pixels + y * image->pitch + x * image->format->BytesPerPixel;
            swap (p[0], p[2]);
        }
    }*/

    /* main code to write jpeg data */
    while (cinfo.next_scanline < cinfo.image_height) {      
        row_pointer = (JSAMPROW) &in[cinfo.next_scanline * im_width*3];
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

	struct image retval;
	retval.len = OUTBUFFER_SIZE - cinfo.dest->free_in_buffer;
	retval.data = cinfo.dest->next_output_byte - retval.len;

	return retval;
}


void send_image(struct mg_connection *c, JSAMPLE *in_buffer) {
	struct image out_image = encode_jpeg(in_buffer);

	char *buf = calloc(out_image.len + 256, 1);

	int headersize = snprintf(buf, 256,
			"--BOUM\r\n"
			"Content-type: image/jpeg\r\n"
			"Content-length: %i\r\n"
			"\r\n",
			out_image.len);
	memcpy(buf + headersize, out_image.data, out_image.len);
	mg_send(c, buf, headersize + out_image.len);
}


static void ev_handler(struct mg_connection *c, int ev, void *p) {
	switch(ev) {
		case MG_EV_CLOSE:
			printf("Connection terminee: %p\n", c);
            remove_conn(c);
			break;
		case MG_EV_SEND:
			printf("Message envoye (%p): %i\n", c, *(int*)p);
			break;
		case MG_EV_HTTP_REQUEST:
			; // Empty statement, because C standard requires it. Go figure...
			struct http_message *hm = (struct http_message *) p;
			char uri[100];
			strncpy(uri, hm->uri.p, hm->uri.len);
			uri[hm->uri.len] = '\0';
			printf("Requete URI (%p): %s\n", c, uri);
			if (hm->uri.len == 1 && strncmp(uri, "/", 1) == 0) {
				// Showing index.html
				char reply[100] = "<html><body><img src='./stream.mjpg' style='width: 800px'/></body></html>";
				mg_printf(c,
						  "HTTP/1.0 200 OK\r\n"
						  "Server: PhDStudentSweat/0.8\r\n"
						  "Content-type: text/html\r\n"
						  "Content-length: %d\r\n"
						  "\r\n"
						  "%s",
						  (int) strlen(reply), reply);
				return;
			} 
			if (strncmp(uri + hm->uri.len - 4, "mjpg", 4) != 0) {
				printf("URI Inconnu: %s\n", uri);
				mg_printf(c,
						  "HTTP/1.0 404 NOT FOUND\r\n"
						  "Server: PhDStudentSweat/0.8\r\n"
						  "Content-type: text/html\r\n"
						  "Content-length: 0\r\n"
						  "\r\n");
				return;
			}
            add_conn(c);
			char firstpacket[] = "HTTP/1.0 200 OK\r\n"
					   "Content-type: multipart/x-mixed-replace; boundary=BOUM\r\n"
					   "\r\n";
			mg_send(c, firstpacket, sizeof(firstpacket) - 1);

			break;
		default:
			break;
	}
}


int main(void) {
    struct mg_connection *nc;
	struct mg_mgr mgr;

    mg_mgr_init(&mgr, NULL);
    nc = mg_bind(&mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(nc);

    /* For each new connection, execute ev_handler in a separate thread */
    //mg_enable_multithreading(nc);

    printf("Starting multi-threaded server on port %s\n", s_http_port);
	int target_ts = 0;
    for (;;) {
        int ts = mg_mgr_poll(&mgr, 1000);

        for (int i = 0; i < MAX_CONN; ++i) {
            if (ts > target_ts && tc[i] != NULL) {
                JSAMPLE *in_buffer = calloc(im_width * im_height, 3);
                in_buffer[800*10 + 10 + 0] = 255;
                in_buffer[800*10 + 10 + 1] = 255;
                in_buffer[800*10 + 10 + 2] = 255;

                send_image(tc[i], in_buffer);
                target_ts = ts + 1;
            }
        }
    }
    mg_mgr_free(&mgr);

    return 0;
}
