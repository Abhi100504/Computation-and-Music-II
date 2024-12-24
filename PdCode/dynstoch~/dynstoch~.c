/****************************************************
 *  This code is proposed in Chapter 13 of          *
 *  "Designing Audio Objects for Max/MSP and Pd"    *
 *  by Eric Lyon.  The algorithm for dynamic        *
 *	stochastic synthesis was presented by Iannis    *
 *	Xenakis in his book "Formalized Music."         *   
 ****************************************************/

/* Obligatory Pd header */

#include "m_pd.h"

/* Needed for random() */

#include <stdlib.h>
#include <math.h>

/* Define the maximum number of waveform extrema */

#define MAX_EXTREMITIES (64)

/* The object structure */

typedef struct _dynstoch
{
   t_object obj;
   t_float x_f;
	float *extremities; // extremities
	long countdown; // current sample countdown
	long countdown_points; // how many samples in countdown
	float *segment_durs; // durations stored in seconds
	int extremities_count; // how many extremities
	int current_segment; // which segment are we on
	float minseg; // minimum segment in seconds
	float maxseg; // maximum segment in seconds
	float freq; // frequency 
	long total_samps; // full size of waveform
	float sr; // current sampling rate
	float x_devo; // deviation of extremities 
	float y_devo; // deviation for durations 
	float minseg_samps; // minimum segment size in samples
	float maxseg_samps; // maximum segment size in samples
	float minfreq; // minimum frequency
	float maxfreq; // maximum frequency
	long minsamps; // minimum total samps
	long maxsamps; // maximum total samps
	long counter; // temp test variable
	short firsttime; // flag for initialization

	// new implementations
    float cauchy_t;          // t parameter for Cauchy distribution
    float *primary_x_pos;    // Primary walk positions for duration
    float *primary_y_pos;    // Primary walk positions for amplitude  
    int use_second_order;    // Toggle for second order mode
} t_dynstoch;

/* The class declaration */

static t_class *dynstoch_class;

/* Function prototypes */

void *dynstoch_new(t_symbol *s, short argc, t_atom *argv);
t_int *dynstoch_perform(t_int *w);
void dynstoch_dsp(t_dynstoch *x, t_signal **sp, short *count);
float dynstoch_rand(float min, float max);
void dynstoch_initwave(t_dynstoch *x);
void dynstoch_transpose(t_dynstoch *x, t_floatarg tfac);
void dynstoch_setfreq(t_dynstoch *x, t_floatarg freq);
void dynstoch_freqbounds(t_dynstoch *x, t_floatarg minf, t_floatarg maxf);
void dynstoch_free(t_dynstoch *x);
void dynstoch_tilde_setup(void);

// new functions
float dynstoch_cauchy_rand(t_dynstoch *x, float min, float max);
void dynstoch_second_order(t_dynstoch *x, t_floatarg f);
void dynstoch_cauchy_t(t_dynstoch *x, t_floatarg t);

/* Replace attributes in the Max/MSP version */

void dynstoch_x_deviation(t_dynstoch *x, t_floatarg xdev);
void dynstoch_y_deviation(t_dynstoch *x, t_floatarg ydev);

/* The class setup routine */

void dynstoch_tilde_setup(void)
{
	t_class *c;
	dynstoch_class = class_new(gensym("dynstoch~"),(t_newmethod)dynstoch_new,(t_method)dynstoch_free,
		sizeof(t_dynstoch),0,A_GIMME,0);
	c = dynstoch_class;
	CLASS_MAINSIGNALIN(dynstoch_class, t_dynstoch, x_f);
	class_addmethod(c, (t_method)dynstoch_dsp, gensym("dsp"), A_CANT, 0);	
	class_addmethod(c, (t_method)dynstoch_initwave, gensym("initwave"), 0);
	class_addmethod(c, (t_method)dynstoch_setfreq, gensym("setfreq"), A_FLOAT, 0);
	class_addmethod(c, (t_method)dynstoch_freqbounds, gensym("freqbounds"), A_FLOAT,A_FLOAT,0);
	class_addmethod(c, (t_method)dynstoch_x_deviation, gensym("x_deviation"), A_FLOAT, 0);
	class_addmethod(c, (t_method)dynstoch_y_deviation, gensym("y_deviation"), A_FLOAT, 0);
	
	class_addmethod(c, (t_method)dynstoch_second_order, gensym("second_order"), A_FLOAT, 0);
	class_addmethod(c, (t_method)dynstoch_cauchy_t, gensym("cauchy_t"), A_FLOAT, 0);

	post("dynstoch~ with Cauchy distribution and second-order walks");
}


void dynstoch_x_deviation(t_dynstoch *x, t_floatarg xdev)
{
	x->x_devo = xdev;
}

void dynstoch_y_deviation(t_dynstoch *x, t_floatarg ydev)
{
	x->y_devo = ydev;
}


/* The new instance routine */

void *dynstoch_new(t_symbol *s, short argc, t_atom *argv)
{
	int i;
	
    t_dynstoch *x = (t_dynstoch *) pd_new(dynstoch_class);
    outlet_new(&x->obj, gensym("signal"));
    outlet_new(&x->obj, gensym("signal"));
	
	/* Allocate space for the extremities */
	
	x->extremities = (float *) getbytes((MAX_EXTREMITIES + 1) *  sizeof(float));
	for(i = 0; i < MAX_EXTREMITIES + 1; i++){
		x->extremities[i] = 0.0;
	}
	x->segment_durs = (float *) getbytes(MAX_EXTREMITIES * sizeof(float));

    x->primary_x_pos = (float *)getbytes(MAX_EXTREMITIES * sizeof(float));
    x->primary_y_pos = (float *)getbytes(MAX_EXTREMITIES * sizeof(float));

	x->minseg = 0.0001;
	x->maxseg = 0.2;
	x->extremities_count = 12;
	x->freq = 440.0;
	x->x_devo = 0.001;
	x->y_devo = 0.001;
	x->countdown_points = 10;
	x->countdown = 10;
	x->minfreq = 50.0;
	x->maxfreq = 10000.0;
	x->firsttime = 1;
	
    x->cauchy_t = 1.0;
    x->use_second_order = 0;

	/* Set initial frequency boundaries */
	
	dynstoch_freqbounds(x, 100.0, 800.0);
	
    return x;
}



/* The rand utility function */

float dynstoch_rand(float min, float max)
{
	return min + (((rand()%32768)/32767.0) * (max-min));
}

float dynstoch_cauchy_rand(t_dynstoch *x, float min, float max) {
    float t = x->cauchy_t;
    float z = dynstoch_rand(0.0, 1.0);
    float cauchy_val = t * tan(M_PI * (z - 0.5));
    float range = max - min;
    float scaled = min + (cauchy_val - (-t)) * range / (2 * t);
    return scaled;
}

/* The transpose method */

void dynstoch_transpose(t_dynstoch *x, t_floatarg tfac)
{
	int i;
	if(tfac <= 0 ) return;
	
	for(i = 0; i < x->extremities_count; i++){
		x->segment_durs[i] /= tfac;
	}
}

/* The setfreq method */

void dynstoch_setfreq(t_dynstoch *x, t_floatarg freq)
{
	float totalsamp =  (x->sr / freq);
	int sampcount = 0;
	long segdur = totalsamp / (float)x->extremities_count;
	int i;
	long slop = totalsamp - (segdur * x->extremities_count);
	if(freq < x->minfreq || freq > x->maxfreq){
		return;
	}
	for(i = 0; i < x->extremities_count; i++){
		x->segment_durs[i] = segdur;
	}
	if(slop > 1){
		i = 0;
		while(slop--){
			x->segment_durs[i++]++;
			i %= x->extremities_count;
		}
	}
	for(i = 0; i < x->extremities_count; i++){
		sampcount += x->segment_durs[i];
	}
	x->freq = x->sr / (float) sampcount;
	x->total_samps = sampcount;
}

/* The nudge routine */

void dynstoch_nudge(t_dynstoch *x)
{
	/*
	 This is the function that alters the waveform at the
	 end of each period.
	 */
	int i;
	float devx, devy;
	long maxsamps = x->maxsamps;
	long minsamps = x->minsamps;
	long actual_total;
	int sampdiff; // how many points need to be added or subtracted from period
	x->total_samps = 0;
	x->counter = 0;
	if( x->x_devo || x->y_devo) {
	
		for(i = 0; i < x->extremities_count; i++){
            if(x->use_second_order) {
				// FIRST WALK
				// Generate step using Cauchy distribution
				float primary_step_x = dynstoch_cauchy_rand(x, -x->x_devo, x->x_devo);
				float primary_step_y = dynstoch_cauchy_rand(x, -x->y_devo, x->y_devo);
				
				// Update primary positions
				x->primary_x_pos[i] += primary_step_x;
				x->primary_y_pos[i] += primary_step_y;
				
				// Mirror primary walk positions if needed
				if(x->primary_x_pos[i] < -x->x_devo) 
					x->primary_x_pos[i] = -2*x->x_devo - x->primary_x_pos[i];
				if(x->primary_x_pos[i] > x->x_devo) 
					x->primary_x_pos[i] = 2*x->x_devo - x->primary_x_pos[i];
				if(x->primary_y_pos[i] < -x->y_devo) 
					x->primary_y_pos[i] = -2*x->y_devo - x->primary_y_pos[i];
				if(x->primary_y_pos[i] > x->y_devo) 
					x->primary_y_pos[i] = 2*x->y_devo - x->primary_y_pos[i];

				// SECOND WALK
				// Generate steps for secondary walk based on primary positions
				float secondary_step_x = x->primary_x_pos[i] * x->x_devo;
				float secondary_step_y = x->primary_y_pos[i] * x->y_devo;
				
				// Update secondary positions (this is where the second walk happens)
				x->segment_durs[i] += secondary_step_x;  
				x->extremities[i] += secondary_step_y;
			} else {
                // Original first-order behavior with Cauchy
                devx = dynstoch_cauchy_rand(x, -x->x_devo, x->x_devo);
                devy = dynstoch_cauchy_rand(x, -x->y_devo, x->y_devo);
                
                x->segment_durs[i] += devx;
                x->extremities[i] += devy;
            }
			
			/* Prevent extremities from wandering into denorm territory */
			
			if(fabs(x->extremities[i]) < 0.000001){
				x->extremities[i] = 0.0;
			}
			
			/* Mirror segment durations when they go out of range */
			
			if(x->segment_durs[i] < x->minseg_samps){
				x->segment_durs[i] = 2*x->minseg_samps - x->segment_durs[i];
			}
			if(x->segment_durs[i] > x->maxseg_samps/x->extremities_count){
				x->segment_durs[i] = 2*(x->maxseg_samps/x->extremities_count) - x->segment_durs[i];
			}
			x->total_samps += x->segment_durs[i];
			
			/* Mirror the point according to Iannis Xenakis's suggestion for out-of-range extremities */
			
			if(x->extremities[i] > 1.0){
				x->extremities[i] = 2.0 - x->extremities[i]; 
			}
			else if(x->extremities[i] < -1.0)
			{
				x->extremities[i] = -2.0 - x->extremities[i]; // mirror
			}
		}
        
        /* Mirror total duration if outside frequency bounds */
        actual_total = x->total_samps;
		if(x->total_samps > maxsamps){
			actual_total = 2*maxsamps - actual_total;
            float scale_factor = (float)actual_total / x->total_samps;
            for(i = 0; i < x->extremities_count; i++) {
                x->segment_durs[i] *= scale_factor;
            }
		} 
		else if(x->total_samps < minsamps) {
			actual_total = 2*minsamps - actual_total;
            float scale_factor = (float)actual_total / x->total_samps;
            for(i = 0; i < x->extremities_count; i++) {
                x->segment_durs[i] *= scale_factor;
            }
		}
		x->total_samps = actual_total;
	}

	/* Check that durations do not drop below the minimum length */
	
	for(i=0; i < x->extremities_count; i++){
		if(x->segment_durs[i] < x->minseg_samps){
			x->segment_durs[i] = x->minseg_samps;
		}
	}
	
	/* Calculate the total sample count of the waveform */
	
	x->total_samps = 0;
	for(i=0; i < x->extremities_count; i++){
		x->total_samps += x->segment_durs[i];
	}
	
	x->freq = x->sr/ x->total_samps;
	x->extremities[x->extremities_count] = x->extremities[0];
	x->countdown = (long)x->segment_durs[0];
	x->countdown_points = x->countdown;
	x->current_segment = 0;
	if(x->countdown_points <= 0){
		x->countdown_points = 8;
	}
}

/* The freebounds method */

void dynstoch_freqbounds(t_dynstoch *x, t_floatarg minf, t_floatarg maxf)
{
	if(!x->sr || !minf || !maxf){
		return;
	}
	x->maxsamps = x->sr / minf;
	x->minsamps = x->sr / maxf;
}

/* The initwave method */

void dynstoch_initwave(t_dynstoch *x)
{
	int i;
	if(!x->sr){
		return;
	}
	x->total_samps = x->sr / x->freq;
	x->minseg = 4.0 / x->sr;
	x->maxseg = 1.0 / 45.0;
	x->minseg_samps = 1;
	x->maxseg_samps = x->maxseg * x->sr;
	
	for(i = 0; i < x->extremities_count; i++){
		x->extremities[i] = dynstoch_rand(-1.0, 1.0);
		x->segment_durs[i] = x->total_samps / x->extremities_count;

        x->primary_x_pos[i] = 0.0;  // Initialize primary walk positions
        x->primary_y_pos[i] = 0.0;
	}
	
	x->extremities[x->extremities_count] = x->extremities[0];
	x->countdown = x->segment_durs[0];
	x->countdown_points = x->countdown;
	x->current_segment = 0;
}

/* The perform routine */

t_int *dynstoch_perform(t_int *w)
{
	t_dynstoch *x = (t_dynstoch *) (w[1]);
	float *output = (t_float *)(w[2]);
	float *frequency = (t_float *)(w[3]);
	int n = w[4];
	float frac;
	int current_segment = x->current_segment;
	float segpoints = x->segment_durs[x->current_segment];
	float *extremities = x->extremities;
	float e1, e2;
	float sample;
	e1 = extremities[current_segment];
	e2 = extremities[current_segment + 1];
	while(n--){
		if(x->countdown <= 0){
			// last point
			++current_segment;
			if(current_segment == x->extremities_count){
				
				/* 
				 We have reached the end of the period, so we
				 nudge the waveform and start again. 
				 */
				
				dynstoch_nudge(x);
				current_segment = 0;
			}
			
			/* Advance to the next extremities point */
			
			else {
				e1 = extremities[current_segment];
				e2 = extremities[current_segment + 1];
				segpoints = x->segment_durs[current_segment];
				x->countdown_points = segpoints; // coerced to long
				x->countdown = x->countdown_points;
			}
		}

		frac = (float)x->countdown / (float)x->countdown_points;
		sample = e1 + frac * (e2 - e1);
		
		/* Keep sample values legal */
		
		if(fabs(sample) < 0.000001){
			sample = 0.0;
		}
		if(fabs(sample) > 2.0){
			sample = 0.0;
		}
		*output++ = sample;
		*frequency++ = x->freq;
		x->countdown--;
		++x->counter;
	}
	x->current_segment = current_segment;
	return w + 5;
}

/* The free memory function */

void dynstoch_free(t_dynstoch *x)
{
	freebytes(x->extremities, (MAX_EXTREMITIES + 1) * sizeof(float));
	freebytes(x->segment_durs, MAX_EXTREMITIES * sizeof(float));

    freebytes(x->primary_x_pos, MAX_EXTREMITIES * sizeof(float));
    freebytes(x->primary_y_pos, MAX_EXTREMITIES * sizeof(float));
}

/* The DSP method */

void dynstoch_dsp(t_dynstoch *x, t_signal **sp, short *count)
{
	if(sp[0]->s_sr){
		x->sr = sp[0]->s_sr;
		x->maxsamps = x->sr / x->minfreq;
		x->minsamps = x->sr / x->maxfreq;
		if(x->firsttime){
			dynstoch_initwave(x);
			x->firsttime = 0;
		}
		
		/* Skip "extra" input vector  provided by MAINSIGNALIN() */
		
		dsp_add(dynstoch_perform, 4, x, sp[1]->s_vec, sp[2]->s_vec, sp[0]->s_n);
	}
}

void dynstoch_second_order(t_dynstoch *x, t_floatarg f) {
    x->use_second_order = (f != 0);
}

void dynstoch_cauchy_t(t_dynstoch *x, t_floatarg t) {
    if(t <= 0) {
        post("cauchy_t must be positive");
        return;
    }
    x->cauchy_t = t;
}