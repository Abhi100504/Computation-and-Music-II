/****************************************************
 *   This code is explicated in Chapter 4 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required header files */

#include "m_pd.h" 
#include "math.h" 

/* The class pointer */

static t_class *vdelay_class;

/* The object structure */

typedef struct _vdelay {
	t_object obj; // The Pd object
	t_float x_f; // for converting floats to signals
	float sr; // sampling rate
	float maximum_delay_time; // maximum delay time
	long delay_length; // length of the delay line in samples
	long delay_bytes; // length of delay line in bytes
	float *delay_line; // the delay line itself
	float delay_time; // current delay time
	float feedback; // feedback multiplier
	long write_index; // write point in delay line
	long read_index; // read point in delay line
	short delaytime_connected; // inlet connection status
	short feedback_connected; // inlet connection status
} t_vdelay;

/* Function prototypes */

void *vdelay_new(t_symbol *s, short argc, t_atom *argv);
void vdelay_dsp(t_vdelay *x, t_signal **sp, short *count);
t_int *vdelay_perform(t_int *w);
void vdelay_free(t_vdelay *x);

/* The object setup function */

void vdelay_tilde_setup (void)
{	
	vdelay_class = class_new(gensym("vdelay~"),(t_newmethod)vdelay_new,(t_method)vdelay_free,sizeof(t_vdelay),0,A_GIMME,0);
	CLASS_MAINSIGNALIN(vdelay_class, t_vdelay, x_f);
	class_addmethod(vdelay_class, (t_method)vdelay_dsp, gensym("dsp"), A_CANT, 0);
	post("vdelay~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *vdelay_new(t_symbol *s, short argc, t_atom *argv)
{
	int i; 
	
	/* Initialize parameters */
	
	float delmax = 100.0, deltime = 100.0, feedback = 0.1;
	
	/* Instantiate a new vdelay~ object */
	
	t_vdelay *x = (t_vdelay *) pd_new(vdelay_class);
	
	/* Create two additional signal inlets */
	
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	
	/* Create one outlet */
	
    outlet_new(&x->obj, gensym("signal"));
	
	/* Programmatically discover the sampling rate */
	
	x->sr = sys_getsr(); 
	
	/* Read optional user parameters */
	
	
	if(argc >= 1){ delmax = atom_getfloatarg(0, argc, argv); }
	if(argc >= 2){ deltime = atom_getfloatarg(1, argc, argv); }
	if(argc >= 3){ feedback = atom_getfloatarg(2, argc, argv); }
	
	// ex
	if(argc == 0){ post("We have set the values to delmax = 100.0 ms, deltime = 100.0 ms, feedback = 0.1"); }
	/* Set the  maximum delay if necessary */	
	
	if(delmax <= 0){
		delmax = 250.0;
	}
	
	/* Convert from ms to secs */
	
	x->maximum_delay_time = delmax * 0.001;
	
	/* Store initial delay time */
	
	x->delay_time = deltime; 
	
	/* Check that the delay times is legal */
	
	if(x->delay_time > delmax || x->delay_time <= 0.0){
		pd_error(x, "illegal delay time: %f, reset to 1 ms", x->delay_time);
		x->delay_time = 1.0;
	}
	
	/* Allocate memory for the delay line */
	
	x->delay_length = x->sr * x->maximum_delay_time + 1;
	x->delay_bytes = x->delay_length * sizeof(float);
	x->delay_line = (float *) getbytes(x->delay_bytes);
	if(x->delay_line == NULL){
		pd_error(x, "vdelay~: cannot allocate %d bytes of memory", x->delay_bytes);
		return NULL;
	}
	
	/* Clear the delayline */
	
	for(i = 0; i < x->delay_length; i++){
		x->delay_line[i] = 0.0;
	}
	
	/* Store the feedback parameter */
	
	x->feedback = feedback;
	
	/* Initialize the write index */
	
	x->write_index = 0;
	
	/* Return a pointer to the object */
	
	return x;
}

/* The free routine */

void vdelay_free(t_vdelay *x)
{
	freebytes(x->delay_line, x->delay_bytes);
}



/* The perform routine */

t_int *vdelay_perform(t_int *w)
{
	t_vdelay *x = (t_vdelay *) (w[1]);
	t_float *input = (t_float *) (w[2]);
	t_float *delaytime = (t_float *) (w[3]);
	t_float *feedback = (t_float *) (w[4]);
	t_float *output = (t_float *) (w[5]);
	int n = w[6];

	/* Dereference components from the object structure */
	
	float sr = x->sr;
	float *delay_line = x->delay_line;
	long read_index = x->read_index;
	long write_index = x->write_index;
	long delay_length = x->delay_length;
	short delaytime_connected = x->delaytime_connected;
	short feedback_connected = x->feedback_connected;
	float delaytime_float = x->delay_time;
	float feedback_float = x->feedback;	
	
	/* Local variables */
	
	float fraction;
	float fdelay;
	float samp1, samp2;
	long idelay;
	float srms = sr / 1000.0;
	float out_sample;
	
	/* Perform the DSP loop */
	
	while(n--){
		
		/* 
		 Process the delay time as either signal or float input.
		 Note that this branching strategy was used in the Max/MSP
		 version of this code. However it is not necessary in the Pd
		 version, and the code could be improved by stripping it out.
		 */
		
		if(delaytime_connected){
			fdelay = *delaytime++ * srms;
		}
		else { fdelay = delaytime_float * srms; }
		while(fdelay < 0){
			fdelay += delay_length;
		}
		
		/* Truncate the delay time */
		
		idelay = floor(fdelay);
		fraction = fdelay - idelay;
		
		/* Calculate the read index and keep it within legal range */
		
		read_index = write_index - idelay;
		while(read_index < 0){
			read_index += delay_length;
		}
		
		/* Do the lookup with linear interpolation */
		
		samp1 = delay_line[read_index];
		samp2 = delay_line[(read_index + 1) % delay_length];
		out_sample = samp1 + fraction * (samp2-samp1);
		
		/* 
		 Process the feedback as either signal or float input.
		 Again, this branching is not necessary in the Pd version. 
		 */
		
		if(feedback_connected) {
			delay_line[write_index++] = *input++ + out_sample * *feedback++;
		}
		else { 
			delay_line[write_index++] = *input++ + out_sample * 
			feedback_float;
		}		
		
		/* Output the delayed sample */
		
		*output++ = out_sample;
		
		/* Keep the write index within legal range */
		
		if(write_index >= delay_length){
			write_index -= delay_length;
		}
	}
	
	/* Copy the incremented write index back to its object component */
	
	x->write_index = write_index;
	
	/* Return the next address on the DSP chain */
	
	return w + 7;
}

/* The DSP routine */

void vdelay_dsp(t_vdelay *x, t_signal **sp, short *count)
{
	int i;
	int oldbytes = x->delay_bytes;
	
	/* Store inlet connection states */
	
	x->delaytime_connected = 1;
	x->feedback_connected = 1;
	if(x->sr != sp[0]->s_sr){
		x->sr = sp[0]->s_sr; 
		x->delay_length = x->sr * x->maximum_delay_time + 1;
		x->delay_bytes = x->delay_length * sizeof(float);
		
		/* Resize memory - only safe if memory has previously been allocated */
		
		x->delay_line = (float *) resizebytes((void *)x->delay_line,oldbytes,x->delay_bytes);
		
		/* Test that sufficient memory was returned */
		
		if(x->delay_line == NULL){
			pd_error(x, "vdelay~: cannot realloc %d bytes of memory", x->delay_bytes);
			return;
		}
		
		/* Clear the delay line */
		
		for(i = 0; i < x->delay_length; i++){ 
			x->delay_line[i] = 0.0;
		}
		
		/* Reset the write point */
		
		x->write_index = 0; 
	}
	/* Add vdelay~ to the Pd DSP chain */
	
	dsp_add(vdelay_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec,
			sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
}