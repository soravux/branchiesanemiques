// Inspired by:
// https://github.com/cesanta/mongoose/tree/master/examples/multithreaded_restful_server
// http://www.andrewewhite.net/wordpress/2010/04/07/simple-cc-jpeg-writer-part-2-write-to-buffer-in-memory/
// https://github.com/lvsn/gel3014/blob/master/BaseStation/main.py
// https://github.com/brad/swftools/blob/master/lib/jpeg.c

#include <time.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mongoose.h"
#include <jpeglib.h>

// Networking
static const char *s_http_port = "8000";
static struct mg_mgr *mgr;

#define MAX_CONN 8
static struct mg_connection *tc[MAX_CONN] = {0};

static void add_conn(struct mg_connection *c) {
    for (int i = 0; i < MAX_CONN; ++i) {
        if (tc[i] == NULL) {
            tc[i] = c;
            break;
        }
    }
}

static void remove_conn(struct mg_connection *c) {
    for (int i = 0; i < MAX_CONN; ++i) {
        if (tc[i] == c) {
            tc[i] = NULL;
        }
    }
}


// Image (JPEG) compression
// OUTBUFFER_SIZE minimum: 2k, otherwise some markers won't fit and it's going to wreak havoc
#define OUTBUFFER_SIZE 0x1000
static int im_width;
static int im_height;


struct image {
	JOCTET *data;
	int len;
};


struct my_mem_destination_mgr {
  struct jpeg_destination_mgr pub;
  unsigned char ** outbuffer;	/* target buffer */
  unsigned long * outsize;
  unsigned char * newbuffer;	/* newly allocated buffer */
  JOCTET * buffer;		/* start of buffer */
  size_t bufsize;
};


void init_buffer(struct jpeg_compress_struct *cinfo) {
    struct my_mem_destination_mgr *dmgr = (struct my_mem_destination_mgr*)(cinfo->dest);
    dmgr->buffer = (JOCTET*)malloc(OUTBUFFER_SIZE);
    if (!dmgr->buffer) {
        perror("malloc");
        printf("Out of memory!\n");
        exit(1);
    }
	dmgr->bufsize = OUTBUFFER_SIZE;
    ((struct jpeg_destination_mgr*)dmgr)->next_output_byte = dmgr->buffer;
    ((struct jpeg_destination_mgr*)dmgr)->free_in_buffer = OUTBUFFER_SIZE;
}

boolean empty_buffer(struct jpeg_compress_struct *cinfo) {
    struct my_mem_destination_mgr *dmgr = (struct my_mem_destination_mgr*)(cinfo->dest);
    int current_len = ((struct jpeg_destination_mgr*)dmgr)->next_output_byte - dmgr->buffer;

	dmgr->bufsize *= 2;
    dmgr->buffer = realloc(dmgr->buffer, dmgr->bufsize);
    ((struct jpeg_destination_mgr*)dmgr)->next_output_byte = dmgr->buffer + current_len;
    ((struct jpeg_destination_mgr*)dmgr)->free_in_buffer = dmgr->bufsize - current_len;

    return FALSE;
}

void term_buffer() {
	//struct jpeg_destination_mgr *dmgr = (struct jpeg_destination_mgr*)(cinfo->dest);
	//datalen = OUTBUFFER_SIZE - dmgr->free_in_buffer;
	//dmgr->free_in_buffer = 0;
}

struct image encode_jpeg(JSAMPLE *in) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr       jerr;
    //struct jpeg_destination_mgr dmgr;
	struct my_mem_destination_mgr dmgr;
	memset(&cinfo, 0, sizeof(cinfo));
	memset(&jerr, 0, sizeof(jerr));
	memset(&dmgr, 0, sizeof(dmgr));

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    ((struct jpeg_destination_mgr*)&dmgr)->init_destination    = init_buffer;
    ((struct jpeg_destination_mgr*)&dmgr)->empty_output_buffer = empty_buffer;
    ((struct jpeg_destination_mgr*)&dmgr)->term_destination    = term_buffer;

    cinfo.dest = (struct jpeg_destination_mgr*)&dmgr;

    cinfo.image_width      = im_width;
    cinfo.image_height     = im_height;
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_RGB;

    jpeg_set_defaults(&cinfo);
	cinfo.dct_method = JDCT_IFAST;
    jpeg_set_quality(&cinfo, 75, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer;

    /* main code to write jpeg data */
    while (cinfo.next_scanline < cinfo.image_height) {      
        row_pointer = (JSAMPROW) &in[cinfo.next_scanline * im_width*3];
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

	struct image retval;
 	retval.len = ((struct jpeg_destination_mgr*)&dmgr)->next_output_byte - dmgr.buffer;
	retval.data = dmgr.buffer;

    free(cinfo.client_data);

	return retval;
}


void send_image(struct mg_connection *c, struct image out_image) {
	char *buf = calloc(out_image.len + 256, 1);

	int headersize = snprintf(buf, 256,
			"--BOUM\r\n"
			"Content-type: image/jpeg\r\n"
			"Content-length: %i\r\n"
			"\r\n",
			out_image.len);
	memcpy(buf + headersize, out_image.data, out_image.len);
	mg_send(c, buf, headersize + out_image.len);

	free(out_image.data);
	free(buf);
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
				char reply[100] = "<html><body><img src='./stream.mjpg'/></body></html>";
				mg_printf(c,
						  "HTTP/1.1 200 OK\r\n"
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
						  "HTTP/1.1 404 NOT FOUND\r\n"
						  "Server: PhDStudentSweat/0.8\r\n"
						  "Content-type: text/html\r\n"
						  "Content-length: 0\r\n"
						  "\r\n");
				return;
			}
            add_conn(c);
			char firstpacket[] = "HTTP/1.1 200 OK\r\n"
					   "Content-type: multipart/x-mixed-replace; boundary=BOUM\r\n"
					   "\r\n";
			mg_send(c, firstpacket, sizeof(firstpacket) - 1);

			break;
		default:
			break;
	}
}


/* //////////////////////////////////////////// //
// ///////// FONCTIONS PUBLIQUES ////////////// //
// //////////////////////////////////////////// */
void envoyer_image(unsigned char *in_buffer, int width, int height) {
    // L'image doit etre de format RGB, 1 octet par canal,
    // sous format RGBRGBRGB[...], Row major (gauche-droite, puis haut-bas)
    im_width = width;
    im_height = height;
	struct image out_image = encode_jpeg(in_buffer);
    for (int i = 0; i < MAX_CONN; ++i) {
        if (tc[i] != NULL) {
            send_image(tc[i], out_image);
        }
    }
}


void init_reseau(void) {
    struct mg_connection *nc;
	mgr = malloc(sizeof(struct mg_mgr));

    mg_mgr_init(mgr, NULL);
    nc = mg_bind(mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(nc);
}


void pomper_evenements() {
    for (int i = 0; i < 10; ++i) {
        mg_mgr_poll(mgr, 10);
    }
}


void liberer_reseau() {
    mg_mgr_free(mgr);
}
/* //////////////////////////////////////////// //
// ///////// FONCTIONS PUBLIQUES FIN ////////// //
// //////////////////////////////////////////// */


int main(void) {
    init_reseau();

    /* For each new connection, execute ev_handler in a separate thread */
    //mg_enable_multithreading(nc);

    int my_width = 800;
    int my_height = 600;

    printf("Demarrage du serveur sur le port %s.\n", s_http_port);
	int target_ts = 0;

    for (;;) {
        pomper_evenements();

        // A chaque ~2 secondes
        if (target_ts < (int)time(NULL)) {

            // Faire une image a la volee
            JSAMPLE *in_buffer = calloc(my_width * my_height, 3);
            for (int i = 0; i < 50000; ++i) {
                in_buffer[rand() % (my_width * my_height * 3)] = rand() % 256;
            }
            envoyer_image(in_buffer, my_width, my_height);

            //target_ts = (int)time(NULL) + 1; // Slower
            target_ts = 0; // Faster
        }
    }

    liberer_reseau(mgr);

    return 0;
}
