/****************************************************
 *   This code is explicated in Chapter 6 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required header files */

#include "m_pd.h" 
#include "stdlib.h"
#include "time.h"

/* Define maximum sequence length */

#define MAX_SEQUENCE 1024

/* The class pointer */

static t_class *retroseq_class;

/* The object structure */

typedef struct _retroseq
{
	t_object obj; // the Pd object
	t_float x_f;// internally convert floats to signals
	float *f_sequence; // store sequence of frequency values
	float *d_sequence; // store sequence of duration values
	int f_sequence_length; // length of frequency sequence
	int d_sequence_length; // length of duration sequence
	int counter; // countdown the note in samples
	int f_position; // position in frequency sequence
	int d_position; // position in duration sequence
	float sr; // sampling rate
	float current_value; // stores current frequency (or whatever)
	int current_duration_samples; // current duration in samples
	float duration_factor; // get samples from duration, sr and tempo
	float tempo; // tempo in BPM
	void *list_outlet; // ADSR list
	void *list_clock; // clock for non-signal events
	t_atom *adsr_list; // holds ADSR
	float sustain_amplitude; // ADSR sustain amplitude
	float *adsr; // ADSR data
	float *adsr_out; // ADSR data
	short elastic_sustain; // flag for scaled envelope
	void *bang_outlet; // start-of-sequence bang outlet
	void *bang_clock; // clock for the bang
	float *tmp_permutation; // work space for permuting sequence
	void *f_plist_outlet; // outlet for permuted frequencies
	void *d_plist_outlet; // outlet for permuted durations
	t_atom *pseq_list; // holds permuted lists	
	short manual_override; // toggle manual override
	short trigger_sent; // user sent a bang
} t_retroseq;

/* Function prototypes */

void *retroseq_new(t_symbol *s, short argc, t_atom *argv);
t_int *retroseq_perform(t_int *w);
void retroseq_dsp(t_retroseq *x, t_signal **sp, short *count);
void retroseq_free(t_retroseq *x);
void retroseq_list(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_tempo(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_durlist(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_freqlist(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_send_adsr(t_retroseq *x);
void retroseq_adsr(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_sustain_amplitude(t_retroseq * x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_elastic_sustain(t_retroseq * x, t_symbol *msg, short argc, t_atom *argv);
void retroseq_send_bang(t_retroseq *x);
void retroseq_assist (t_retroseq *x, void *b, long msg, long arg, char *dst);
void retroseq_permute(float *sequence, float *permutation, int len);
void retroseq_shuffle_freqs(t_retroseq *x);
void retroseq_shuffle_durs(t_retroseq *x);
void retroseq_shuffle(t_retroseq *x);
void retroseq_bang(t_retroseq *x);
void retroseq_manual_override(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv);

/* The object setup function */

void retroseq_tilde_setup(void)
{
	t_class *c;
	retroseq_class = class_new(gensym("retroseq~"), (t_newmethod)retroseq_new, (t_method)retroseq_free, 
	sizeof(t_retroseq), 0,A_GIMME,0);
	CLASS_MAINSIGNALIN(retroseq_class, t_retroseq, x_f);
	c = retroseq_class;
	class_addmethod(c, (t_method)retroseq_dsp, gensym("dsp"),0, A_CANT, 0);
	class_addmethod(c, (t_method)retroseq_list, gensym("list"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_durlist, gensym("durlist"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_freqlist, gensym("freqlist"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_tempo, gensym("tempo"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_adsr, gensym("adsr"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_sustain_amplitude, gensym("sustain_amplitude"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_elastic_sustain,gensym( "elastic_sustain"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_shuffle_freqs, gensym("shuffle_freqs"),0);
	class_addmethod(c, (t_method)retroseq_shuffle_durs, gensym("shuffle_durs"),0);
	class_addmethod(c, (t_method)retroseq_shuffle, gensym("shuffle"),0);
	class_addmethod(c, (t_method)retroseq_manual_override, gensym("manual_override"), A_GIMME, 0);
	class_addmethod(c, (t_method)retroseq_bang, gensym("bang"),0);
	post("retroseq~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *retroseq_new(t_symbol *s, short argc, t_atom *argv)
{
	/* Instantiate the object */
	
	t_retroseq *x = (t_retroseq *)pd_new(retroseq_class);

	/* Create a signal outlet */
	
    outlet_new(&x->obj, gensym("signal"));
	
	/* 
	 Now create various message outlets. Note that the order
	 of outlet creation is different to that of Max/MSP. 
	 */
	
    x->list_outlet = outlet_new(&x->obj, gensym("list"));
    x->bang_outlet = outlet_new(&x->obj, gensym("bang"));
    x->f_plist_outlet = outlet_new(&x->obj, gensym("list"));
	x->d_plist_outlet = outlet_new(&x->obj, gensym("list"));
	
	/* 
	 In case the sampling rate is zero, we temporarily
	 set it to a non-zero value. 
	 */
	
	x->sr =  sys_getsr();
	if(!x->sr){
		x->sr = 44100.0;
	}
	
	/* Instantiate the clocks */
	
	x->list_clock = clock_new(x,(t_method)retroseq_send_adsr);
	x->bang_clock = clock_new(x,(t_method)retroseq_send_bang);
	
	/* Allocate memory for the arrays */
	
	x->f_sequence = (float *)getbytes(MAX_SEQUENCE*sizeof(float));
	x->d_sequence = (float *)getbytes(MAX_SEQUENCE*sizeof(float) );
	x->adsr_list = (t_atom *) getbytes(10 * sizeof(t_atom));
	x->adsr_out = (float *) getbytes(10 * sizeof(float));
	x->adsr = (float *) getbytes(4 * sizeof(float));
	x->tmp_permutation = (float *)getbytes(MAX_SEQUENCE * sizeof(float));
	x->pseq_list = (t_atom *) getbytes(MAX_SEQUENCE * sizeof(t_atom));	
	
	/* Check for memory allocation failure */
	
	if(x->f_sequence == NULL || x->d_sequence == NULL){
		post("retroseq~: memory allocation failure");
		
		/* In case of memory problems return an invalid object */

		return NULL;
	}
	
	/* Seed the pseudo-random number generator with the time of day */
	
	srand(clock()); 
	
	/* 
	 Set the position to the beginning of the sequence namely zero. 
	 This is a very important step. If we do not initialize the position, 
	 it is likely to be a random very large number, which could cause an 
	 instant crash as soon as we use it to access the sequence array. 
	 */
	
	x->f_position = 0;
	x->d_position = 0;
	x->elastic_sustain = 0;
	
	/* Set the tempo */
	
	x->tempo = 60.0;
	x->duration_factor = x->sr/1000.0 ; // default tempo is 60
	
	/* Set an initial sequence */	
	
	x->f_sequence_length = 3;
	x->d_sequence_length = 3;
	x->f_sequence[0] = 440;
	x->f_sequence[1] = 550;
	x->f_sequence[2] = 660;
	x->d_sequence[0] = 250;
	x->d_sequence[1] = 125;
	x->d_sequence[2] = 125;
	
	/* Initialize the ADSR envelope */
	
	x->adsr_out[0] = 0.0;
	x->adsr_out[1] = 0.0;
	x->adsr_out[2] = 1.0;
	x->adsr_out[8] = 0.0;
	x->adsr[0] = 20;
	x->adsr[1] = 50;
	x->adsr[2] = 100;
	x->adsr[3] = 50;
	x->sustain_amplitude = 0.7;
	x->current_value = x->f_sequence[0];
	x->counter = x->d_sequence[0] * x->sr/ 1000.0 ;	
	
	/* Return a pointer to the new object */	
	
	return x;
}

/* The tempo method */

void retroseq_tempo(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	float old_tempo;
	float t;
	
	/* Check validity of user input */
	
	if(argc == 1){
		t = atom_getfloatarg(0, argc,argv);
	} 
	else {
		return;
	}
	if( t <= 0 ){
		pd_error(x, "retroseq~: tempo must be greater than zero");
		return;
	}
	
	/* Save the old tempo */
	
	old_tempo = x->tempo;
	
	/* Store the new tempo */
	
	x->tempo = t;
	x->duration_factor = (60.0/x->tempo)*(x->sr/1000.0);
	
	/* Rescale current counter to new tempo */
	
	x->counter *= old_tempo / x->tempo;
}

/* The free memory routine */

void retroseq_free(t_retroseq *x)
{
	freebytes(x->d_sequence, MAX_SEQUENCE*sizeof(float));
	freebytes(x->f_sequence, MAX_SEQUENCE*sizeof(float));
	freebytes(x->adsr, 4 * sizeof(float));
	freebytes(x->adsr_out, 10 * sizeof(float));
	freebytes(x->adsr_list, 10 * sizeof(t_atom));
	freebytes(x->tmp_permutation, MAX_SEQUENCE * sizeof(float));
	freebytes(x->pseq_list, MAX_SEQUENCE * sizeof(t_atom));
	clock_free(x->list_clock);
	clock_free(x->bang_clock);
}

/* The list method */

void retroseq_list(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i, j;
	float *f_sequence = x->f_sequence;
	float *d_sequence = x->d_sequence;
	
	/* Reject lists with an odd number of members */
	
	if(argc % 2){
		pd_error(x, "retroseq~: odd number of arguments!");
		return;
	}
	
	/* Reject zero member lists */
	
	if(argc < 2){
		return;		
	}
	
	/* Protect against oversized lists */
	
	if(x->f_sequence_length >= MAX_SEQUENCE){
		pd_error(x, "retroseq~: sequence is too long");
		return;
	} 		

	/* Set the sequence length based on the list size */

	x->f_sequence_length = argc / 2;
	x->d_sequence_length = argc / 2;
	
	/* Read and store the sequences */
	
	for (i=0, j=0; i < argc; i += 2, j++) {
	
		f_sequence[j] = atom_getfloatarg(i,argc,argv);
		d_sequence[j] = atom_getfloatarg(i+1,argc,argv);
		if(d_sequence[j] <= 0){
			pd_error(x, "retroseq~: %f is an illegal duration value. Reset to 100 ms.",d_sequence[j] );
			d_sequence[j] = 100.0;
		}
	}
	
	/* Initialize the sequence positions */
	
	x->f_position = x->f_sequence_length - 1;
	x->d_position = x->d_sequence_length - 1;
}



/* The duration shuffle method */

void retroseq_shuffle_durs(t_retroseq *x)
{
	int i;
	
	/* Deference object components */
	
	float *tmp_permutation = x->tmp_permutation;
	float *d_sequence = x->d_sequence;
	int d_sequence_length = x->d_sequence_length;
	t_atom *pseq_list = x->pseq_list;
	
	/* Call the permute function on the duration sequence */
	
	retroseq_permute(d_sequence, tmp_permutation, d_sequence_length);
	
	/* Copy the sequence to a list of Atoms */
	
	for( i = 0; i < d_sequence_length; i++ ){
		SETFLOAT(pseq_list+i,d_sequence[i]);
	}
	
	/* Send the array of atoms to the duration sequence list outlet */
	
	outlet_list(x->d_plist_outlet,0,d_sequence_length,pseq_list);
	
}

/* The frequency shuffle method */

void retroseq_shuffle_freqs(t_retroseq *x)
{
	int i;
	float *tmp_permutation = x->tmp_permutation;
	float *f_sequence = x->f_sequence;
	int f_sequence_length = x->f_sequence_length;
	t_atom *pseq_list = x->pseq_list;
	
	retroseq_permute(f_sequence, tmp_permutation, f_sequence_length);
	
	for( i = 0; i < f_sequence_length; i++ ){
		SETFLOAT(pseq_list+i,f_sequence[i]);
	}
	
	outlet_list(x->f_plist_outlet,0,f_sequence_length,pseq_list);
	
}

/* The dual shuffle convenience method */

void retroseq_shuffle(t_retroseq *x)
{
	/* Call both shuffle methods */
	
	retroseq_shuffle_freqs(x);
	retroseq_shuffle_durs(x);
}

/* The permute utility function */

void retroseq_permute(float *sequence, float *permutation, int len)
{
	int cnt = 0;
	int rpos;
	float tmp;
	int i;
	int tlen = len;
	
	/* Reorder the elements in the sequence array */
	
	while(tlen > 1){
		rpos = rand() % tlen ;
		permutation[cnt++] = sequence[rpos];
		tmp = sequence[rpos]; 
		sequence[rpos] = sequence[tlen - 1];
		sequence[tlen - 1] = tmp;
		--tlen;
	}
	permutation[len - 1] = sequence[0];
	for( i = 0; i < len; i++ ){
		sequence[i] = permutation[i];
	}
}

/* The bang method */

void retroseq_send_bang(t_retroseq *x) 
{
	outlet_bang(x->bang_outlet);
}

/* The frequency list entry method */

void retroseq_freqlist( t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	float *f_sequence = x->f_sequence;
	
	if( argc < 2 ){
		return;
	}
	
	/* Read the input length directly from argc */
	
	x->f_sequence_length = argc;
	
	/* Protect against illegal lengths */
	
	if( x->f_sequence_length  >= MAX_SEQUENCE ){
		pd_error(x, "retroseq~: frequency sequence is too long");
		return;
	} 
	
	/* Read the message list into the frequency sequence */
	
	for (i=0 ; i < argc; i++) {
		f_sequence[i] = atom_getfloatarg(i,argc,argv);
	}
	
	/* Set the position to the end of the sequence */
	
	x->f_position = x->f_sequence_length - 1;
}

/* The duration list entry method */

void retroseq_durlist( t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	float *d_sequence = x->d_sequence;
	
	if( argc < 2 )
		return;
	x->d_sequence_length = argc;
	
	if( x->d_sequence_length  >= MAX_SEQUENCE ){
		pd_error(x, "retroseq~: duration sequence is too long");
		return;
	} 
	
	for (i=0 ; i < argc; i++) {
		d_sequence[i] = atom_getfloatarg(i,argc,argv);
	}
	x->f_position = x->d_sequence_length - 1;
}

/* The send ADSR method */

void retroseq_send_adsr(t_retroseq *x)
{
	/* Dereference object components */
	
	t_atom *adsr_list = x->adsr_list;
	float *adsr = x->adsr;
	float *adsr_out = x->adsr_out;
	short elastic_sustain = x->elastic_sustain;
	int d_position = x->d_position;
	float *d_sequence = x->d_sequence;
	float tempo = x->tempo;
	
	/* Local variables */
	
	float note_duration_ms;
	float duration_sum;	
	float rescale ;
	int i;
	
	/* 
	 Read the current duration from the duration sequence
	 and convert it to milliseconds.
	 */
	
	note_duration_ms = d_sequence[d_position] * (60.0/tempo);
	
	/* Populate the ADSR output array */
	
	adsr_out[4] = adsr_out[6] = x->sustain_amplitude;
	
	/* Copy the attack duration */
	
	adsr_out[3] = adsr[0];
	
	/* Copy the decay duration */
	
	adsr_out[5] = adsr[1];
	
	/* Copy the release duration */
	
	adsr_out[9] = adsr[3];
	
	/* If manual override is in effect, disregard the sequence duration */

	if(x->manual_override){

		/* Swap in user sustain */
		
		adsr_out[7] = adsr[2]; 
	} 
	else {
		
		/* Reset the sustain duration if elastic, with a minimum of 1 ms. */
		
		if(elastic_sustain){
			adsr_out[7] = note_duration_ms - (adsr[0]+adsr[1]+adsr[3]);
			if(adsr_out[7] < 1.0){ // minimum sustain of 1 millisecond
				adsr_out[7] = 1.0 ;
			}
		} 
		
		/* Otherwise use the stored sustain duration */
		
		else {
			adsr_out[7] = adsr[2]; // user specified sustain duration
		}
		
		/* Calculate the total duration of the envelope */
		
		duration_sum = adsr_out[3] + adsr_out[5] + adsr_out[7] + adsr_out[9];
		
		/* 
		 If the note duration is shorter than the envelope duration
		 then rescale envelope.
		 */
		
		if(duration_sum > note_duration_ms){
			rescale = note_duration_ms / duration_sum ;
			adsr_out[3] *= rescale;
			adsr_out[5] *= rescale;
			adsr_out[7] *= rescale;
			adsr_out[9] *= rescale;
		}
	}
	
	/* Build the ADSR output list */
	
	for(i = 0; i < 10; i++){ // build list
		SETFLOAT(adsr_list+i,adsr_out[i]);
	}
	
	/* Send the ADSR data list to the ADSR list outlet */
	
	outlet_list(x->list_outlet,NULL,10,adsr_list); // send list through outlet
}


/* The elastic sustain method */

void retroseq_elastic_sustain(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	
	/* Set the elastic sustain flag */
	
	if(argc >= 1){
		x->elastic_sustain = (short) atom_getfloatarg(0,argc,argv);
	}
}

/* The sustain amplitude method */

void retroseq_sustain_amplitude(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	
	/* Set the amplitude level for the sustain portion of the envelope */

	if(argc >= 1){
		x->sustain_amplitude = atom_getfloatarg(0,argc,argv);
	}
}

/* The ADSR input method */

void retroseq_adsr(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	float *adsr = x->adsr;
	int i;
	
	if( argc < 4 ){
		pd_error(x, "not enough parameters for adsr (should be 4)");
		return;
	}
	
	/* Read user input data for the ADSR */
	
	for (i=0 ; i < 4; i++) {
		adsr[i] = atom_getfloatarg(i,argc,argv);
		if( adsr[i] < 1.0 ){
			adsr[i] = 1.0;
		}
	}
}

/* The manual override method */

void retroseq_manual_override(t_retroseq *x, t_symbol *msg, short argc, t_atom *argv)
{
	/* Set the flag for manual override */
	
	if(argc >= 1){
		x->manual_override = (short) atom_getfloatarg(0,argc,argv);
	}
}

/* The bang method */

void retroseq_bang(t_retroseq *x)
{
	/* Trigger a new event in manual override mode */
	
	x->trigger_sent = 1;
}

/* The perform method */

t_int *retroseq_perform(t_int *w)
{
	t_retroseq *x = (t_retroseq *) (w[1]);
	float *out = (t_float *)(w[2]);
	int n = w[3];
	int f_sequence_length = x->f_sequence_length;
	int d_sequence_length = x->d_sequence_length;
	int counter = x->counter;
	int f_position = x->f_position;
	int d_position = x->d_position;
	float *f_sequence = x->f_sequence;
	float *d_sequence = x->d_sequence;
	float current_value = x->current_value;
	float duration_factor = x->duration_factor;
	short manual_override = x->manual_override;
	short trigger_sent = x->trigger_sent;
	
	/* The manual override DSP loop */
	
	if( manual_override ){
		while(n--){
			
			/* Process an input trigger (bang) */
			
			if( trigger_sent ){
				trigger_sent = 0;
				++f_position;
				if( f_position >= f_sequence_length ){
					f_position = 0;
					
					/*
					 When at the end of the sequence, Call the method 
					 bound to the bang_clock. The bound method
					 is retroseq_send_bang(). The method will be called
					 at a lower priority, outside of this DSP routine. 
					 */
					
					clock_delay(x->bang_clock,0);
				}
				current_value = f_sequence[f_position];
				
				/*
				 Send the ADSR as a list out the object's list outlet.
				 The method bound to list_clock is retroseq_send_adsr().
				 */
				
				clock_delay(x->list_clock,0);
			}
			
			/* Send the current envelope value as a signal */
			
			*out++ = current_value;
		}
	} 
	else {
		
		/* The normal DSP loop */
		
		while(n--){
			if(! counter--){
				
				/* 
				 When the counter hits zero, advance the sequence and
				 send outlet messages as shown above. 
				 */
				
				if(++f_position >= f_sequence_length){
					f_position = 0;
					
					/* Send a bang */
					
					clock_delay(x->bang_clock,0); 
				}
				if(++d_position >= d_sequence_length){
					d_position = 0;
				}
				counter = d_sequence[d_position] * duration_factor;
				current_value = f_sequence[f_position];
				
				/* Send list output */

				
				clock_delay(x->list_clock,0); 
			}
			*out++ = current_value;
		}
	}
	
	/* Restore local variables to their corresponding object components */
	
	x->trigger_sent = trigger_sent;
	x->current_value = current_value;
	x->counter = counter;
	x->f_position = f_position;
	x->d_position = d_position;	
	
	/* Return the next address on the DSP chain */
	
	return w + 4;
}

/* The DSP method */

void retroseq_dsp(t_retroseq *x, t_signal **sp, short *count)
{
	
	/* Adjust tempo data if the sampling rate changes */
	
	if( x->sr != sp[0]->s_sr ){
		if( ! sp[0]->s_sr ){
			pd_error(x, "zero sampling rate!");
			return;
		}
		
		/* Rescale the countdown */
		
		x->counter *= x->sr/sp[0]->s_sr;
		
		/* Rescale the duration factor */
		
		x->duration_factor *=  sp[0]->s_sr/x->sr;
		
		/* Store the new sampling rate */
		
		x->sr = sp[0]->s_sr;
	}
	
	/* 
	 Attach retroseq~ to the DSP chain. Note that we skip the
	 automatically generated signal inlet. 
	 */
	
	dsp_add(retroseq_perform, 3, x, sp[1]->s_vec, sp[0]->s_n);
}


