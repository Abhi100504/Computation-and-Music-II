/****************************************************
 *   This code is explicated in Chapter 5 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required header files */

#include "m_pd.h"
#include "math.h"

/* Define constants and defaults */

#define OSCIL_DEFAULT_TABLESIZE 8192
#define OSCIL_DEFAULT_HARMS 10
#define OSCIL_MAX_HARMS 1024
#define OSCIL_DEFAULT_FREQUENCY 440.0
#define OSCIL_DEFAULT_WAVEFORM "sine"
#define OSCIL_MAX_TABLESIZE 1048576
#define OSCIL_NOFADE 0
#define OSCIL_LINEAR 1
#define OSCIL_POWER 2

/* The class pointer */

static t_class *oscil_class;

/* The object structure */

typedef struct _oscil
{
	t_object obj; // the Pd object
	t_float x_f; // internally convert floats to signals
	long table_length; // length of wavetable
	float *wavetable; // wavetable
	float *amplitudes; // list of amplitudes for each harmonic
	t_symbol *waveform; // the waveform used currently
	long harmonic_count; // number of harmonics
	float phase; // wavetable phase
	float si; // sampling increment
	float si_factor; // factor for generating the sampling increment
	long bl_harms; // number of harmonics for band limited waveforms
	float piotwo; // pi over two
	float twopi; // two pi
	float sr; // sampling rate
	long wavetable_bytes;// number of bytes stored in wavetable
	long amplitude_bytes;// number of bytes stored in amplitude table
	float xfade_duration; // duration of the crossfade in milliseconds
	int xfade_samples; // number of samples in the crossfade
	int xfade_countdown; // current sample count for the crossfade
	short xfadetype; // the crossfade type 
	short firsttime; // initialization flag for the crossfade
	float *old_wavetable;// older wave table
	short dirty;// flag that wavetable is dirty

} t_oscil;

/* Function prototypes */

void *oscil_new(t_symbol *s, short argc, t_atom *argv);
t_int *oscil_perform(t_int *w);
void oscil_dsp(t_oscil *x, t_signal **sp, short *count);
void oscil_build_waveform( t_oscil *x );
void oscil_mute(t_oscil *x, t_floatarg toggle);
void oscil_assist(t_oscil *x, void *b, long msg, long arg, char *dst);
void oscil_sine(t_oscil *x);
void oscil_triangle(t_oscil *x);
void oscil_square(t_oscil *x);
void oscil_sawtooth(t_oscil *x);
void oscil_pulse(t_oscil *x);
void oscil_list (t_oscil *x, t_symbol *msg, short argc, t_atom *argv);
void oscil_fadetime (t_oscil *x, t_floatarg fade_ms);
void oscil_fadetype(t_oscil *x, t_floatarg ftype);
void oscil_free(t_oscil *x);

/* The object setup function */

void oscil_tilde_setup (void)
{
	t_class * c;
	oscil_class = class_new( gensym("oscil~"), (t_newmethod) oscil_new, (t_method)oscil_free, sizeof(t_oscil), 0, A_GIMME, 0);
	CLASS_MAINSIGNALIN(oscil_class, t_oscil, x_f);
	c = oscil_class;
	class_addmethod(c,(t_method)oscil_dsp, gensym("dsp"),0);
	class_addmethod(c,(t_method)oscil_sine, gensym("sine"), 0);
	class_addmethod(c,(t_method)oscil_triangle, gensym("triangle"), 0);
	class_addmethod(c,(t_method)oscil_square, gensym("square"), 0);
	class_addmethod(c,(t_method)oscil_sawtooth, gensym("sawtooth"), 0);
	class_addmethod(c,(t_method)oscil_pulse, gensym("pulse"), 0);
	class_addmethod(c,(t_method)oscil_list, gensym("list"), A_GIMME, 0);
	class_addmethod(c,(t_method)oscil_fadetime, gensym("fadetime"), A_FLOAT, 0);
	class_addmethod(c,(t_method)oscil_fadetype, gensym("fadetype"), A_FLOAT, 0);
	post("oscil~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *oscil_new(t_symbol *s, short argc, t_atom *argv)
{
	float init_freq; 
	
	/* Instantiate the object */
	
	t_oscil *x = (t_oscil *) pd_new(oscil_class);
	
	// Create the second inlet for phase control
    inlet_new(&x->obj, &x->obj.ob_pd, &s_signal, &s_signal);

	/* Create one outlet */
	
	outlet_new(&x->obj, gensym("signal"));
		
	/* Turn on the firsttime flag */
	
	x->firsttime = 1;
	
	/* Initialize the object with default parameters */ 
	
	init_freq = OSCIL_DEFAULT_FREQUENCY;
	x->table_length = OSCIL_DEFAULT_TABLESIZE;
	x->bl_harms = OSCIL_DEFAULT_HARMS;
	x->waveform = gensym(OSCIL_DEFAULT_WAVEFORM);
	x->dirty = 0;
	
	/* Read any user-specified parameters */

	if(argc >= 1){ init_freq = atom_getfloatarg(0, argc, argv); }
	if(argc >= 2){ x->table_length = (long) atom_getintarg(1, argc, argv); }
	if(argc >= 3){ x->waveform = atom_getsymbolarg(2, argc, argv); }
	if(argc >= 4){ x->bl_harms = (long) atom_getintarg(3, argc, argv); }
	
	
	/* 
	 Check for illegal values supplied by the user, and replace all
	 illegal values with defaults. 
	 */
	
	if(fabs(init_freq) > 1000000){
		init_freq = OSCIL_DEFAULT_FREQUENCY;
		post("bad freq reset");
	}
	if(x->table_length < 4 || x->table_length > OSCIL_MAX_TABLESIZE){
		x->table_length = OSCIL_DEFAULT_TABLESIZE;
	}
	if(x->bl_harms < 1 || x->bl_harms > OSCIL_MAX_HARMS){
		post("bad harms reset from: %d", x->bl_harms);
		x->bl_harms = OSCIL_DEFAULT_HARMS;

	}
	
	/* Generate 2π and π/2 */
	
	x->twopi = 8.0 * atan(1.0);
	x->piotwo = 2. * atan(1.0);
	
	/* 
	 Allocate dynamic memory. Note that we do not check if the memory returned was valid. 
	 This code could be improved by checking that the returned memory pointer is not NULL.
	 */
	
	x->wavetable_bytes = x->table_length * sizeof(float);
	x->wavetable = (float *) getbytes(x->wavetable_bytes);
	x->amplitude_bytes = OSCIL_MAX_HARMS * sizeof(float);
	x->amplitudes = (float *) getbytes(x->amplitude_bytes);
	x->old_wavetable = (float *) getbytes(x->wavetable_bytes);
	
	/*
	 Initialize the phase to zero. It is essential that we initialize the phase variable, 
	 otherwise it could contain some random value outside 0 - (table_length - 1), 
	 in which case the first lookup from the wavetable would crash the program.
	 */
	
	x->phase = 0;
	
	/* 
	 Programmatically determine the sampling rate, but note that we will need
	 to recheck this value in the DSP method. 
	 */
	
	x->sr = sys_getsr(); 
	
	/* Note that it is dangerous to divide by the x->sr below, which could in 
	 certain cases be zero. This code could be improved by first checking the
	 value of x->sr. 
	 */
	
	x->si_factor = (float) x->table_length / x->sr;
	x->si = init_freq * x->si_factor ;
	
	
	/* Initialize fade parameters */
	
	x->xfade_countdown = 0;
	x->xfade_duration = 1000.;
	x->xfade_samples = x->xfade_duration * x->sr / 1000.0;
	x->xfadetype = OSCIL_LINEAR;
	
	/* Branch to the appropriate method to initialize the waveform */
	
	if (x->waveform == gensym("sine")) {
		oscil_sine(x);
	} else if (x->waveform == gensym("triangle")) {
		oscil_triangle(x);
	} else if (x->waveform == gensym("square")) {
		oscil_square(x);
	} else if (x->waveform == gensym("sawtooth")) {
		oscil_sawtooth(x);
	} else if (x->waveform == gensym("pulse")) {
		oscil_pulse(x);
	} else {
		pd_error("%s is not a legal waveform - using sine wave instead", x->waveform->s_name);
		oscil_sine(x);
	}
	
	/* Turn off firsttime flag now that the waveform has been initialized */
	
	x->firsttime = 0;
	
	/* Return a pointer to the object */
	
	return x;
}

/* The sine method */

void oscil_sine(t_oscil *x)
{
	/* No DC component */
	
	x->amplitudes[0] = 0.0;
	
	/* All the energy is in the first harmonic */
	
	x->amplitudes[1] = 1.0;
	
	/* In this special case, there is only one harmonic */
	
	x->harmonic_count = 1;
	
	/* Build the waveform */
	
	oscil_build_waveform(x);
}

/* The triangle method */

void oscil_triangle(t_oscil *x)
{
	int i;
	float sign = 1.0;
	
	/* No DC component */
	
	x-> amplitudes [0] = 0.0; 
	
	/* Set the number of harmonics */
	
	x->harmonic_count = x->bl_harms;
	
	/* 
	 Set the amplitudes, alternating the sign of the harmonic
	 in accordance with the summation formula for a triangle wave.
	 Multiplying an amplitude by -1 is equivalent to a 180 degree
	 phase shift for that harmonic. Notice that we are only using 
	 odd harmonics.
	 */
	
	for( i = 1 ; i < x->bl_harms; i += 2 ){
		x->amplitudes[i] = sign * 1.0/((float)i * (float)i);
		x->amplitudes[i + 1] = 0.0;
		sign *= -1;
	}
	
	/* Build the waveform */
	
	oscil_build_waveform(x);
}

/* The sawtooth method */

void oscil_sawtooth(t_oscil *x)
{
	int i;
	float sign = 1.0;
	
	x->amplitudes[0] = 0.0;
	x->harmonic_count = x->bl_harms;
	
	/* 
	 Set the amplitudes. Notice that we use both
	 even and odd harmonics, alternating the sign.
	 */
	
	for(i = 1 ; i < x->bl_harms; i++){
		x->amplitudes[i] = sign * 1.0/(float)i;
		sign *= -1. ;
	}
	oscil_build_waveform(x);
}

/* The square method */

void oscil_square(t_oscil *x)
{
	int i;
	x-> amplitudes [0] = 0.0;
	x->harmonic_count = x->bl_harms;
	for(i = 1 ; i < x->bl_harms; i += 2){
		x->amplitudes[i] = 1.0/(float)i;
		x->amplitudes[i + 1] = 0.0;
	}
	oscil_build_waveform(x);
}


/* The pulse method */

void oscil_pulse(t_oscil *x)
{
	int i;
	x->amplitudes[0] = 0.0;
	x->harmonic_count = x->bl_harms;
	
	/* 
	 For a pulse wave, all harmonics are
	 equally weighted.
	 */
	
	for(i = 1 ; i < x->bl_harms; i++){
		x->amplitudes[i] = 1.0;
	}
	oscil_build_waveform(x);
}

/* The list method */

void oscil_list (t_oscil *x, t_symbol *msg, short argc, t_atom *argv)
{
	short i;
	int harmonic_count = 0;
	float *amplitudes = x->amplitudes;
	
	/* Read the list of harmonic weightings from the calling message */
	
	for (i=0; i < argc; i++) {
		amplitudes[harmonic_count++] = atom_getfloat(argv + i);
	}
	
	/* Set the harmonic count to the number of weightings read */
	
	x->harmonic_count = harmonic_count;
	oscil_build_waveform(x);
}

/* The utility function to build the waveform */

void oscil_build_waveform(t_oscil *x) {
	float rescale;
	int i, j;
	float max;
	float *wavetable = x->wavetable;
	float *amplitudes = x->amplitudes;
	
	/* Add 1 to the harmonic count to account for the DC component */
	
	int partial_count = x->harmonic_count + 1; 
	int table_length = x->table_length;
	float twopi = x->twopi;
	float *old_wavetable = x->old_wavetable;
	
	/* Copy the current current wavetable to old wavetable */
	
	for(i = 0; i < table_length ; i++){
		old_wavetable[i] = wavetable[i];
	}
	
	/* 
	 Set the dirty flag, so that the oscillator will read from the old
	 wavetable until the new one has been generated.
	 */
	
	x->dirty = 1;
	
	/* 
	 Reset the fade countdown, if the fadetype is not NOFADE, 
	 and this is not first time the function has been called.
	 */
	
	
	if( x->xfadetype  && ! x->firsttime){
		x->xfade_countdown = x->xfade_samples; 
	}	
	
	/* Check for an empty set of weightings */
	
	if(partial_count < 1){
		perror("oscil~: no harmonics specified, waveform not created.");
		return;
	}
	
	/* Check that the user has not specified an all zero function */
	
	max = 0.0;
	for(i = 0; i < partial_count; i++){
		max += fabs(amplitudes[i]);
	}
	if(! max){
		perror("oscil~: all zero function specified, waveform not created.");
		return; 
	}
	
	/* Start building the wavetable with the DC component */
	
	for(i = 0; i < table_length; i++){
		wavetable[i] = amplitudes[0];
	}
	
	/* Sum all specified harmonics into the wavetable */
	
	for(i = 1 ; i < partial_count; i++){
		if(amplitudes[i]){
			for(j = 0; j < table_length; j++){
				
				/* Notice that we are accumulating samples into the wavetable, thus the += operator */
				
				wavetable[j] += amplitudes[i] * sin(twopi * ((float)i * ((float)j/(float)table_length))) ;
			}
		}
	}
	
	/* 
	 Determine the maximum amplitude. Since the waveform is symmetric, 
	 we only look at the first half 
	 */
	
	max = 0.0;
	for(i = 0; i < table_length / 2; i++){
		if(max < fabs(wavetable[i])){
			max = fabs(wavetable[i]) ;
		}
	}
	
	/* The following should never happen but it's easy enough to check */
	
	if(max == 0.0) {
		post("oscil~: weird all zero error - exiting!");
		return;
	}
	
	/* Normalize the waveform to maximum amplitude of 1.0 */
	
	rescale = 1.0 / max ;
	for(i = 0; i < table_length; i++){
		wavetable[i] *= rescale ;
	}
	
	/* Release the new wave table */
	
	x->dirty = 0; 
}

/* The fadetime method */

void oscil_fadetime (t_oscil *x, t_floatarg fade_ms)
{
	/* Check that the fade time is legal */
	
	// if(fade_ms < 0.0 || fade_ms > 600000.0){
	// 	pd_error("%f is not a legal fade time", fade_ms);
	// 	fade_ms = 50.;
	// }
	
	/* Store the fade time duration in milliseconds */
	
	x->xfade_duration = fade_ms;
	
	/* Convert from milliseconds to samples */
	
	x->xfade_samples = x->xfade_duration * x->sr / 1000.0;
}

/* The fade type method */

void oscil_fadetype(t_oscil *x, t_floatarg ftype)
{
	if(ftype < 0 || ftype > 2) {
		perror("unknown type of fade, selecting no fade");
		ftype = 0;
	}
	
	/* Type conversion from float to short */
	
	x->xfadetype = (short) ftype;
}

/* The perform routine */

t_int *oscil_perform(t_int *w)
{
	/* Copy the object pointer to a local variable  */
	
	t_oscil *x = (t_oscil *) (w[1]);
	
	/* Copy the inlet and outlet pointers to local variables */
	
	float *frequency = (t_float *)(w[2]);
	float *phase_in = (t_float *)(w[3]); // added phase in
	float *out = (t_float *)(w[4]); // adjusted value
	
	/* Copy the signal vector size to a local variable */
	
	int n = w[5]; //adjusted value
	
	/* Dereference oscil~ object components */
	
	float si_factor = x->si_factor;
	float si = x->si;
	float phase = x->phase;
	int table_length = x->table_length;
	float *wavetable = x->wavetable;
	float *old_wavetable = x->old_wavetable;
	int xfade_countdown = x->xfade_countdown;
	int xfade_samples = x->xfade_samples;
	short xfadetype = x->xfadetype;
	float piotwo = x->piotwo;
	
	/* local variables */
	
	long iphase;
	float fraction;	
	
	/* Perform the DSP loop */
	
	while (n--) {
		
		/* Calculate the sampling increment */
		
		si = *frequency++ * si_factor;
		

		// new phase factor

 		float phase_offset = *phase_in++ * table_length; 
		float current_phase = phase + phase_offset;

        if(current_phase >= table_length)
            current_phase -= table_length;
        if(current_phase < 0)
            current_phase += table_length;

        iphase = (long)current_phase;






		/* Truncate the local phase */
		
		// iphase = floor(phase);
		
		/* 
		 In case the wavetable is being rebuilt,
		 read from the old copy of the wavetable.
		 */
		
		if(x->dirty){
			*out++ = old_wavetable[iphase];
		} 
		
		/* This block only executed during a crossfade */
		
		else if (xfade_countdown > 0) { 
			fraction = (float)xfade_countdown--/(float)xfade_samples;
			if(xfadetype == OSCIL_LINEAR){
				*out++ = wavetable[iphase] + fraction * 	   
				(old_wavetable[iphase] - wavetable[iphase]);
			} 
			else if (xfadetype == OSCIL_POWER) {
				
				/* 
				 Scale the fraction so that the cos() and sin() 
				 lookups are constrained to the first quadrant 
				 of the Unit Circle.
				 */
				
				fraction *= piotwo; 
				*out++ = sin(fraction) * old_wavetable[iphase] + 
				cos(fraction) * wavetable[iphase];
			}
		}	
		else {
			*out++ = wavetable[iphase];	
		}	
		
		/* Increment the phase */
		
		phase += si;
		
		/* Keep the phase within range of the table length*/
		
		while(phase >= table_length) {
			phase -= table_length;
		}
		while(phase < 0) {
			phase += table_length;
		}
	}
	
	/* Store the current phase and crossfade countdown */
	
	x->xfade_countdown = xfade_countdown;
	x->phase = phase;
	
	/* Return the next address on the DSP chain */
	
	return w + 6; //changed position
}

/* The free routine */

void oscil_free(t_oscil *x)
{
	t_freebytes(x->wavetable, x->wavetable_bytes);
	t_freebytes(x->old_wavetable, x->wavetable_bytes);
	t_freebytes(x->amplitudes, OSCIL_MAX_HARMS * sizeof(float));
}

/* The DSP routine */

void oscil_dsp(t_oscil *x, t_signal **sp, short *count)
{
	
	if(x->sr != sp[0]->s_sr){
		if(! sp[0]->s_sr){
			perror("zero sampling rate!");
			return;
		}
		x->si *= x->sr / sp[0]->s_sr;
		x->sr = sp[0]->s_sr;
		x->si_factor = (float) x->table_length / x->sr;
	}
	
	dsp_add(oscil_perform, 5, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[0]->s_n); // changed for another input
}


