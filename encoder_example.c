
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <vorbis/vorbisenc.h>

#include "encoderstate.h"

#define READ 1024
signed char readbuffer[READ*4+44]; /* out of the data segment, not the stack */

void setup_encoder(OggEncoderState *encoder) {
  vorbis_info_init(&(encoder->vi));
  int ret=vorbis_encode_init_vbr(&(encoder->vi),2,44100,0.1);
  if(ret)exit(1);
}

void add_headers(OggEncoderState *encoder, FILE *fp) {
  /* add a comment */
  vorbis_comment_init(&(encoder->vc));
  vorbis_comment_add_tag(&(encoder->vc),"ENCODER","encoder_example.c");
  vorbis_comment_add_tag(&(encoder->vc),"ARTIST","Rumblesan");
  vorbis_comment_add_tag(&(encoder->vc),"TITLE","Test song");

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init(&(encoder->vd),&(encoder->vi));
  vorbis_block_init(&(encoder->vd),&(encoder->vb));

  /* set up our packet->stream encoder */
  /* pick a random serial number; that way we can more likely build
     chained streams just by concatenation */
  srand(time(NULL));
  ogg_stream_init(&(encoder->os),rand());

  /* Vorbis streams begin with three headers; the initial header (with
     most of the codec setup parameters) which is mandated by the Ogg
     bitstream spec.  The second header holds any comment fields.  The
     third header holds the bitstream codebook.  We merely need to
     make the headers, then pass them to libvorbis one at a time;
     libvorbis handles the additional Ogg bitstream constraints */

  {
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;

    vorbis_analysis_headerout(&(encoder->vd),&(encoder->vc),&header,&header_comm,&header_code);
    ogg_stream_packetin(&(encoder->os),&header); /* automatically placed in its own
                                                    page */
    ogg_stream_packetin(&(encoder->os),&header_comm);
    ogg_stream_packetin(&(encoder->os),&header_code);

    /* This ensures the actual
     * audio data will start on a new page, as per spec
     */
    while(1) {
      int result=ogg_stream_flush(&(encoder->os),&(encoder->og));
      if(result==0)break;
      fwrite(encoder->og.header,1,encoder->og.header_len,fp);
      fwrite(encoder->og.body,1,encoder->og.body_len,fp);
    }

  }
}

float **deinterleave(signed char *data, int read) {

  float **test = malloc(2 * sizeof(float *));
  test[0] = malloc((read / 4) * sizeof(float));
  test[1] = malloc((read / 4) * sizeof(float));

  for(long i = 0; i < read/4; i++){
    test[0][i]=((data[i*4+1]<<8)|
                (0x00ff&(int)data[i*4]))/32768.f;
    test[1][i]=((data[i*4+3]<<8)|
                (0x00ff&(int)data[i*4+2]))/32768.f;
  }

  return test;
}

int write_audio(OggEncoderState *encoder, long samplespc, float **audio, FILE *fp) {
  int finished = 0;

  float **buffer=vorbis_analysis_buffer(&(encoder->vd),READ);

  for (long t = 0; t < samplespc; t+= 1) {
    buffer[0][t] = audio[0][t];
    buffer[1][t] = audio[1][t];
  }
  free(audio[0]);
  free(audio[1]);
  free(audio);
  /* tell the library how much we actually submitted */
  vorbis_analysis_wrote(&(encoder->vd), samplespc);

  /* vorbis does some data preanalysis, then divvies up blocks for
     more involved (potentially parallel) processing.  Get a single
     block for encoding now */
  while(vorbis_analysis_blockout(&(encoder->vd),&(encoder->vb))==1){

    /* analysis, assume we want to use bitrate management */
    vorbis_analysis(&(encoder->vb),NULL);
    vorbis_bitrate_addblock(&(encoder->vb));

    while(vorbis_bitrate_flushpacket(&(encoder->vd),&(encoder->op))){

      /* weld the packet into the bitstream */
      ogg_stream_packetin(&(encoder->os),&(encoder->op));

      /* write out pages (if any) */
      while(!finished){
        int result=ogg_stream_pageout(&(encoder->os),&(encoder->og));
        if(result==0)break;
        fwrite(encoder->og.header,1,encoder->og.header_len,fp);
        fwrite(encoder->og.body,1,encoder->og.body_len,fp);

        /* this could be set above, but for illustrative purposes, I do
           it here (to show that vorbis does know where the stream ends) */

        if(ogg_page_eos(&(encoder->og)))finished=1;
      }
    }
  }
  return finished;
}

int main(){

  OggEncoderState *encoder = malloc(sizeof(OggEncoderState));

  readbuffer[0] = '\0';
  for (int i = 0, founddata = 0; i < 30 && !feof(stdin) && !ferror(stdin); i+= 1) {
      fread(readbuffer,1,2,stdin);
      if ( ! strncmp((char*)readbuffer, "da", 2) ){
        founddata = 1;
        fread(readbuffer,1,6,stdin);
        break;
      }
    }

  setup_encoder(encoder);

  add_headers(encoder, stdout);

  int finished = 0;
  while(!finished){
    long read=fread(readbuffer,1,READ*4,stdin); /* stereo hardwired here */

    float **deint = deinterleave(readbuffer, read);
    int samplespc = read/4;

    finished = write_audio(encoder, samplespc, deint, stdout);
  }

  /* clean up and exit.  vorbis_info_clear() must be called last */

  ogg_stream_clear(&(encoder->os));
  vorbis_block_clear(&(encoder->vb));
  vorbis_dsp_clear(&(encoder->vd));
  vorbis_comment_clear(&(encoder->vc));
  vorbis_info_clear(&(encoder->vi));

  /* ogg_page and ogg_packet structs always point to storage in
     libvorbis.  They're never freed or manipulated directly */

  fprintf(stderr,"Done.\n");
  return(0);
}
