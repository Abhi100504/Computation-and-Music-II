/****************************************************
 *   This code is explicated in Chapter 8 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required Header Files */

#include "m_pd.h" 
#include "stdlib.h" 
#include "math.h" 
#include "string.h"

/* Define trig constants */

#define PI 3.1415926535898
#define TWOPI 6.2831853071796

/* Define status constants */

#define SCRUBBER_EMPTY 0
#define SCRUBBER_FULL 1

/* The class pointer */

static t_class *scrubber_class;

/* The object structure */

typedef struct _scrubber {
	t_object obj; // the Pd object
	t_float x_f; // for internal conversion from float to signal
	float duration_ms; // duration in milliseconds
	long recorded_frames; // how many frames in spectrum
	long framecount; // total frames in spectrum
	long oldframes; // for resizing memory
	long fftsize; // number of bins in a frame
	float **magnitudes; // contains spectral frames
	float **phasediffs; // contains spectral frames
	float *lastphase_in; // maintain phase for frame differences
	float *lastphase_out; // maintain phase for frame accumulation
	float sr; // sampling rate
	float frame_position; // current frame
	float increment; // speed to advance through the spectrum
	short acquire_sample; //flag to begin sampling
	float sync; // where are we in buffer
	short resize; // flag to resize in memory call
	float framedur; // duration in seconds of a single overlapped frame
	float overlap;// overlap factor
	float last_position; //last input position
	short buffer_status; // check if we need to force memory initialization
} t_scrubber;

/* Function prototypes */

void *scrubber_new(t_symbol *s, int argc, t_atom *argv);
void scrubber_dsp(t_scrubber *x, t_signal **sp, short *count);
t_int *scrubber_perform(t_int *w);
void scrubber_assist(t_scrubber *x, void *b, long msg, long arg, char *dst);
void scrubber_init_memory(t_scrubber *x);
void scrubber_sample(t_scrubber *x);
void scrubber_overlap(t_scrubber *x, t_symbol *msg, short argc, t_atom *argv);
void scrubber_resize(t_scrubber *x, t_symbol *msg, short argc, t_atom *argv);
void scrubber_free(t_scrubber *x);
void scrubber_tilde_setup(void);
void scrubber_info(t_scrubber *x);

/* The object setup function */

void scrubber_tilde_setup(void)
{
	t_class *c;
	scrubber_class = class_new(gensym("scrubber~"), (t_newmethod)scrubber_new, (t_method)scrubber_free, sizeof(t_scrubber), 0, A_GIMME, 0);
	c = scrubber_class;
	CLASS_MAINSIGNALIN(scrubber_class, t_scrubber, x_f);
	class_addmethod(c, (t_method)scrubber_dsp, gensym("dsp"), A_CANT, 0);
	class_addmethod(c, (t_method)scrubber_sample, gensym("sample"), 0);
	class_addmethod(c, (t_method)scrubber_info, gensym("info"), 0);
	class_addmethod(c, (t_method)scrubber_resize, gensym("resize"), A_GIMME, 0);
	class_addmethod(c, (t_method)scrubber_overlap, gensym("overlap"), A_GIMME, 0);
	post("scrubber~ from \"Designing Audio Objects\" by Eric Lyon");
}


/* The new instance routine */

void *scrubber_new(t_symbol *s, int argc, t_atom *argv)
{
	t_scrubber *x = (t_scrubber *)pd_new(scrubber_class);
	
	/* Create three additional signal inlets and three signal outlets */
	
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
    outlet_new(&x->obj, gensym("signal"));
	outlet_new(&x->obj, gensym("signal"));
	outlet_new(&x->obj, gensym("signal"));
	
	/* 
	 Ensure that *magitudes pointer is NULL so that memory is allocated with the
	 first scrubber_init_memory() call.
	 */
	
	x->magnitudes = NULL;
	
	/* Set the frame size to 1 for initialization purposes */
	
	x->framecount = 1;
	x->acquire_sample = 0;
	x->fftsize = 1024;
	x->frame_position = 0;
	x->oldframes = 0;

	/* Set duration according to user paramter or by default */

	
	if( argc >= 1){
		x->duration_ms = atom_getfloatarg(0, argc, argv);
	} 
	else{
		
		/* Default buffer size is 5 seconds */
		
		x->duration_ms = 5000.0;
	}
	x->last_position = 0;
	x->overlap = 8.0;	
	x->buffer_status = SCRUBBER_EMPTY;

	/* Initialize sampling rate */
	
	x->sr = sys_getsr();
	
	/* initialize memory */
	
	scrubber_init_memory(x);
	
	/* Return a pointer to the object */
	
	return x;
}

/* The overlap method */

void scrubber_overlap(t_scrubber *x, t_symbol *msg, short argc, t_atom *argv)
{
	/* Store the old overlap value */
	
	float old_overlap = x->overlap;
	if(argc >= 1){
		
		/* Read the new overlap value */
		
		x->overlap = atom_getfloatarg(0,argc,argv);
		
		/* If the user value is bad, restore the old value and exit */
		
		if(x->overlap <= 0){
			post("scrubber~: bad overlap: %f", x->overlap);
			x->overlap = old_overlap;
			return;
		}
		
		/* If the new overlap is different than the old one, reset the memory */
		
		if(x->overlap != old_overlap){
			scrubber_init_memory(x);			
		}
	}
}

/* The resize method */

void scrubber_resize(t_scrubber *x, t_symbol *msg, short argc, t_atom *argv)
{
	/* Store the old size */
	
	float old_size = x->duration_ms;
	
	/* Read the user supplied duration value */
	
	if(argc >= 1){
		x->duration_ms = atom_getfloatarg(0,argc,argv);
	} else {
		return;
	}
	
	/* Exit if the new duration is the same as the old one */
	
	if(old_size == x->duration_ms){
		return;
	}
	
	/* If the input values are good, resize the memory */
	
	if(x->duration_ms > 0.0 && x->sr > 0.0 && x->fftsize > 0.0 && x->overlap > 0.0){
		x->framedur = x->fftsize / x->sr;
		x->oldframes = x->framecount;
		x->framecount = 0.001 * x->duration_ms * x->overlap / x->framedur;
		scrubber_init_memory(x);
	}
	
	/* If the values are bad, restore the old size */
	
	else {
		post("scrubber~: cannot resize to: %f", x->duration_ms);
		x->duration_ms = old_size;
	}
}

/* The sample method */

void scrubber_sample(t_scrubber *x)
{
	/* Turn on the flag to acquire a sample */
	
	x->acquire_sample = 1;
	
	/* Set the frame count to 0 */
	
	x->recorded_frames = 0;
	
	/* Set the buffer state to "empty" */
	
	x->buffer_status = SCRUBBER_EMPTY;
}

/* The memory initialization routine, using standard C lib calls */

void scrubber_init_memory(t_scrubber *x)
{
	long framecount = x->framecount;
	long oldframes = x->oldframes;
	long fftsize = x->fftsize;
	long fftsize2 = fftsize / 2;
	int i;
	long bytesize;
	
	if(framecount <= 0){
		post("bad frame count: %d", framecount);
		return;
	}
	if(fftsize <= 0){
		post("bad size: %d", fftsize);
		return;
	}
	
	/* Set the buffer status to empty, so that any contents will be cleared */
	
	x->buffer_status = SCRUBBER_EMPTY;
	bytesize = framecount * sizeof(float *);
	
	/* If the memory pointer is NULL allocate new memory blocks */
	
	if(x->magnitudes == NULL){
		x->magnitudes = (float **) malloc(bytesize);
		x->phasediffs = (float **) malloc(bytesize);
		bytesize = (fftsize2 + 1) * sizeof(float);
		for(i = 0; i < framecount; i++){
			x->magnitudes[i] = (float *) malloc(bytesize);
			x->phasediffs[i] = (float *) malloc(bytesize);
		}
		x->lastphase_in = (float *) malloc(bytesize);
		x->lastphase_out = (float *) malloc(bytesize);
		memset(x->lastphase_in, 0, bytesize);
		memset(x->lastphase_out, 0, bytesize);
	} 
	
	/* Otherwise resize existing memory blocks */
	
	else {
		
		/* Free old frames */
		
		for(i = 0; i < oldframes; i++){
			free(x->magnitudes[i]);
			free(x->phasediffs[i]);
		}
		
		/* Then resize pointers */
		
		bytesize = framecount * sizeof(float *);
		x->magnitudes = (float **)realloc(x->magnitudes, bytesize);
		x->phasediffs = (float **)realloc(x->phasediffs, bytesize);
		
		/* Allocate new frames */
		
		bytesize = (fftsize2 + 1) * sizeof(float);
		for(i = 0; i < framecount; i++){
			x->magnitudes[i] = (float*)malloc(bytesize);
			x->phasediffs[i] = (float*)malloc(bytesize);
		}
		x->lastphase_in = (float *) realloc(x->lastphase_in, bytesize);
		x->lastphase_out = (float *) realloc(x->lastphase_out, bytesize);
		memset(x->lastphase_in, 0, bytesize);
		memset(x->lastphase_out, 0, bytesize);
	}
	
	/* Store the new frame count */
	
	x->oldframes = framecount;
}


void scrubber_info(t_scrubber *x)
{		
	post("******* scrubber statistics *******");
	post("duration: %f", x->duration_ms);
	post("sample rate: %f", x->sr);
	post("Vector size: %d", x->fftsize);
	post("FFT size: %d", x->fftsize);
	post("overlap: %f", x->overlap);
	post("framecount: %d", x->framecount);
}


/* The perform routine */

t_int *scrubber_perform(t_int *w)
{
	t_scrubber *x = (t_scrubber *) (w[1]); // the object
	t_float *real_in = (t_float *) (w[2]); // real input
	t_float *imag_in = (t_float *) (w[3]); // imaginary input
	t_float *increment = (t_float *) (w[4]); // playback increment
	t_float *position = (t_float *) (w[5]); // playback position
	t_float *real_out = (t_float *) (w[6]); // real output
	t_float *imag_out = (t_float *) (w[7]); // imaginary output
	t_float *sync = (t_float *) (w[8]); // sync signal
	t_int n = w[9]; // signal vector size 
	
	/* Dereference object components */
	
	long framecount = x->framecount;
	long recorded_frames = x->recorded_frames;
	float frame_position = x->frame_position;
	float **magnitudes = x->magnitudes;
	float **phasediffs = x->phasediffs;

	short acquire_sample = x->acquire_sample;
	float last_position = x->last_position;
	float *lastphase_in = x->lastphase_in; // maintain phase for frame deviations
	float *lastphase_out = x->lastphase_out; // maintain phase for frame accumulation
	
	/* Local variables */
	
	float phase_out, mag_out;
	float local_phase, phasediff;
	int i;
	float a, b;
	long iframe_position;
	float sync_val;
	
	/* Set N2 to half of FFT size */
	
	int N2 = n / 2; 

	/* Sample acquisition block */
	
	if(acquire_sample){
		
	/* Analysis part */
		
	/* Compute the sync signal */	
		
		sync_val = (float) recorded_frames / (float) framecount;
		
		/* Convert complex spectrum to polar like Max/MSP cartopol~ */
		
		for ( i = 0; i < N2 + 1; i++ ) {
			a = real_in[i];
			b = ( i == 0 || i == N2 ? 0. : imag_in[i] );
			
			/* Store magnitude */
			
			magnitudes[recorded_frames][i] = hypot(a, b);
			
			/* Store phase and difference from last phase like Max/MSP framedelta~ */
			
			local_phase = -atan2( b, a );
			phasediff = local_phase - lastphase_in[i];
			
			/* Store phase */

			lastphase_in[i] = local_phase; 
			
			/* Unwrap phase like Max/MSP phasewrap~ */
			
			while(phasediff > PI){
				phasediff -= TWOPI;
			}
			while(phasediff < -PI){
				phasediff += TWOPI;
			}

			/* Store the difference between this frame and the last frame */
			
			phasediffs[recorded_frames][i] = phasediff;
		}
		for(i = 0; i < n; i++){
			
			/* Output silence while sampling */
			
			real_out[i] = 0.;
			imag_out[i] = 0.;
			sync[i] = sync_val;
		}
		++recorded_frames;
		if(recorded_frames >= framecount){
			acquire_sample = 0;
			x->buffer_status = SCRUBBER_FULL;
		}
	} 
	else if(x->buffer_status == SCRUBBER_FULL) {
		
		/* Synthesis part */
		
		/* Compute the sync value */
		
		sync_val = frame_position / (float) framecount;
		
		/* If the input position has changed, set the frame lookup position */
		
		if( last_position != *position && *position >= 0.0 && *position <= 1.0 ){
			last_position = *position;
			frame_position = last_position * (float)(framecount - 1);
		}
		
		/* Always increment the frame position */
		
		frame_position += *increment;

		/* Keep the frame position within legal range */
		
		while(frame_position < 0.){
			frame_position += framecount;
		}
		while(frame_position >= framecount){
			frame_position -= framecount;
		}
		
		/* Drop the fractional part of the frame position */
		
		iframe_position = floor(frame_position);
		
		
		for ( i = 0; i < N2+1; i++ ) {
			
			/* Send out the magnitudes */
			
			mag_out = magnitudes[iframe_position][i];
			
			/* Accumulate the phase differences like Max/MSP frameaccum~ */
			
			lastphase_out[i] += phasediffs[iframe_position][i];
			local_phase = lastphase_out[i];
			
			/* Convert to Cartesian representation like Max/MSP poltocar~ */
			
			real_out[i] = mag_out * cos(local_phase);
			imag_out[i] = (i == 0 || i == N2) ? 0.0 : -mag_out * sin(local_phase);
			
			/* Send out the sync value */
			
			sync[i] = sync_val;
		}
		
		/* Zero out the remaining half of the arrays */
		
		for ( i = N2+1; i < n; i++ )
		{
			real_out[i] = 0.;
			imag_out[i] = 0.;

			/* Fill the rest of the sync vector */

			sync[i] = sync_val;
		}
		
	}
	
	/* Output silence if the buffer is empty */
	
	else {
		for(i = 0; i < n; i++){
			real_out[i] = 0.0;
			imag_out[i] = 0.0;
			sync[i] = 0.0;
		}
	}
	
	/* Save state values to corresponding object components */
	
	x->last_position = last_position;
	x->frame_position = frame_position;
	x->acquire_sample = acquire_sample;
	x->recorded_frames = recorded_frames;
	
	/* Return the next address on the DSP chain */
	
	return w + 10;
}

/* The free function*/


void scrubber_free(t_scrubber *x)
{
	int i;
	if(x->magnitudes != NULL){
		for(i = 0; i < x->framecount; i++){
			free(x->magnitudes[i]);
			free(x->phasediffs[i]);
		}
		free(x->magnitudes);
		free(x->phasediffs);
		free(x->lastphase_in);
		free(x->lastphase_out);
	} 
}

/* The DSP method */

void scrubber_dsp(t_scrubber *x, t_signal **sp, short *count)
{
	/* Cannot use s_sr component inside block~ subpatches, so must use sys_getsr() */

	float local_sr = sys_getsr();
	long local_blocksize = sp[0]->s_n;
	float framedur;
	long new_framecount;
	
	/* Do not add scrubber~ to DSP chain if the sampling rate is zero */

	if(!local_sr){
		return;
	}
	framedur = local_blocksize / x->sr;
	new_framecount = 0.001 * x->duration_ms * x->overlap / framedur;
	
	if(x->fftsize != local_blocksize || x->sr != local_sr|| x->framecount != new_framecount) {
		x->fftsize = local_blocksize;
		x->sr = local_sr;
		x->framecount = new_framecount;
		scrubber_init_memory(x);
	}
	dsp_add(scrubber_perform, 9, 
			x,  sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, 
			sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[0]->s_n);
}
