
#include <vorbis/vorbisenc.h>

typedef struct OggEncoderState {

  ogg_stream_state  os;

  ogg_page          og;
  ogg_packet        op;

  vorbis_info       vi;

  vorbis_comment    vc;

  vorbis_dsp_state  vd;
  vorbis_block      vb;

} OggEncoderState;
