/* Copyright (C) 2007-2008 Jean-Marc Valin
   Copyright (C) 2008      Thorvald Natvig
      
   File: resample.c
   Arbitrary resampling code

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

/*
   The design goals of this code are:
      - Very fast algorithm
      - SIMD-friendly algorithm
      - Low memory requirement
      - Good *perceptual* quality (and not best SNR)

   Warning: This resampler is relatively new. Although I think I got rid of 
   all the major bugs and I don't expect the API to change anymore, there
   may be something I've missed. So use with caution.

   This algorithm is based on this original resampling algorithm:
   Smith, Julius O. Digital Audio Resampling Home Page
   Center for Computer Research in Music and Acoustics (CCRMA), 
   Stanford University, 2007.
   Web published at http://www-ccrma.stanford.edu/~jos/resample/.

   There is one main difference, though. This resampler uses cubic 
   interpolation instead of linear interpolation in the above paper. This
   makes the table much smaller and makes it possible to compute that table
   on a per-stream basis. In turn, being able to tweak the table for each 
   stream makes it possible to both reduce complexity on simple ratios 
   (e.g. 2/3), and get rid of the rounding operations in the inner loop. 
   The latter both reduces CPU time and makes the algorithm more SIMD-friendly.
*/

#include <stdlib.h>
#include <math.h>
#include <emmintrin.h>

static inline float inner_product_single(const float *a, const float *b, unsigned int len)
{
   int i;
   float ret;
   __m128 sum = _mm_setzero_ps();
   for (i=0;i<len;i+=8)
   {
      sum = _mm_add_ps(sum, _mm_mul_ps(_mm_loadu_ps(a+i), _mm_loadu_ps(b+i)));
      sum = _mm_add_ps(sum, _mm_mul_ps(_mm_loadu_ps(a+i+4), _mm_loadu_ps(b+i+4)));
   }
   sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
   sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 0x55));
   _mm_store_ss(&ret, sum);
   return ret;
}

static inline float interpolate_product_single(const float *a, const float *b, unsigned int len, const uint oversample, float *frac) {
  int i;
  float ret;
  __m128 sum = _mm_setzero_ps();
  __m128 f = _mm_loadu_ps(frac);
  for(i=0;i<len;i+=2)
  {
    sum = _mm_add_ps(sum, _mm_mul_ps(_mm_load1_ps(a+i), _mm_loadu_ps(b+i*oversample)));
    sum = _mm_add_ps(sum, _mm_mul_ps(_mm_load1_ps(a+i+1), _mm_loadu_ps(b+(i+1)*oversample)));
  }
   sum = _mm_mul_ps(f, sum);
   sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
   sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 0x55));
   _mm_store_ss(&ret, sum);
   return ret;
}

struct SpeexResamplerState_;
typedef struct SpeexResamplerState_ SpeexResamplerState;
typedef int (*resampler_basic_func)(SpeexResamplerState *, uint , const float *, uint *, float *, uint *);
struct SpeexResamplerState_ {
   uint in_rate;
   uint out_rate;
   uint num_rate;
   uint den_rate;
   
   int    quality;
   uint nb_channels;
   uint filt_len;
   uint mem_alloc_size;
   uint buffer_size;
   int          int_advance;
   int          frac_advance;
   float  cutoff;
   uint oversample;
   int          initialised;
   int          started;
   
   /* These are per-channel */
   int  *last_sample;
   uint *samp_frac_num;
   uint *magic_samples;
   
   float *mem;
   float *sinc_table;
   uint sinc_table_length;
   resampler_basic_func resampler_ptr;
         
   int    in_stride;
   int    out_stride;
} ;

static double kaiser12_table[68] = {
   0.99859849, 1.00000000, 0.99859849, 0.99440475, 0.98745105, 0.97779076,
   0.96549770, 0.95066529, 0.93340547, 0.91384741, 0.89213598, 0.86843014,
   0.84290116, 0.81573067, 0.78710866, 0.75723148, 0.72629970, 0.69451601,
   0.66208321, 0.62920216, 0.59606986, 0.56287762, 0.52980938, 0.49704014,
   0.46473455, 0.43304576, 0.40211431, 0.37206735, 0.34301800, 0.31506490,
   0.28829195, 0.26276832, 0.23854851, 0.21567274, 0.19416736, 0.17404546,
   0.15530766, 0.13794294, 0.12192957, 0.10723616, 0.09382272, 0.08164178,
   0.07063950, 0.06075685, 0.05193064, 0.04409466, 0.03718069, 0.03111947,
   0.02584161, 0.02127838, 0.01736250, 0.01402878, 0.01121463, 0.00886058,
   0.00691064, 0.00531256, 0.00401805, 0.00298291, 0.00216702, 0.00153438,
   0.00105297, 0.00069463, 0.00043489, 0.00025272, 0.00013031, 0.0000527734,
   0.00001000, 0.00000000};
static double kaiser10_table[36] = {
   0.99537781, 1.00000000, 0.99537781, 0.98162644, 0.95908712, 0.92831446,
   0.89005583, 0.84522401, 0.79486424, 0.74011713, 0.68217934, 0.62226347,
   0.56155915, 0.50119680, 0.44221549, 0.38553619, 0.33194107, 0.28205962,
   0.23636152, 0.19515633, 0.15859932, 0.12670280, 0.09935205, 0.07632451,
   0.05731132, 0.04193980, 0.02979584, 0.02044510, 0.01345224, 0.00839739,
   0.00488951, 0.00257636, 0.00115101, 0.00035515, 0.00000000, 0.00000000};

static double kaiser8_table[36] = {
   0.99635258, 1.00000000, 0.99635258, 0.98548012, 0.96759014, 0.94302200,
   0.91223751, 0.87580811, 0.83439927, 0.78875245, 0.73966538, 0.68797126,
   0.63451750, 0.58014482, 0.52566725, 0.47185369, 0.41941150, 0.36897272,
   0.32108304, 0.27619388, 0.23465776, 0.19672670, 0.16255380, 0.13219758,
   0.10562887, 0.08273982, 0.06335451, 0.04724088, 0.03412321, 0.02369490,
   0.01563093, 0.00959968, 0.00527363, 0.00233883, 0.00050000, 0.00000000};
   
static double kaiser6_table[36] = {
   0.99733006, 1.00000000, 0.99733006, 0.98935595, 0.97618418, 0.95799003,
   0.93501423, 0.90755855, 0.87598009, 0.84068475, 0.80211977, 0.76076565,
   0.71712752, 0.67172623, 0.62508937, 0.57774224, 0.53019925, 0.48295561,
   0.43647969, 0.39120616, 0.34752997, 0.30580127, 0.26632152, 0.22934058,
   0.19505503, 0.16360756, 0.13508755, 0.10953262, 0.08693120, 0.06722600,
   0.05031820, 0.03607231, 0.02432151, 0.01487334, 0.00752000, 0.00000000};

struct FuncDef {
   double *table;
   int oversample;
};
      
static struct FuncDef _KAISER12 = {kaiser12_table, 64};
#define KAISER12 (&_KAISER12)
static struct FuncDef _KAISER10 = {kaiser10_table, 32};
#define KAISER10 (&_KAISER10)
static struct FuncDef _KAISER8 = {kaiser8_table, 32};
#define KAISER8 (&_KAISER8)
static struct FuncDef _KAISER6 = {kaiser6_table, 32};
#define KAISER6 (&_KAISER6)

struct QualityMapping {
   int base_length;
   int oversample;
   float downsample_bandwidth;
   float upsample_bandwidth;
   struct FuncDef *window_func;
};


/* This table maps conversion quality to internal parameters. There are two
   reasons that explain why the up-sampling bandwidth is larger than the 
   down-sampling bandwidth:
   1) When up-sampling, we can assume that the spectrum is already attenuated
      close to the Nyquist rate (from an A/D or a previous resampling filter)
   2) Any aliasing that occurs very close to the Nyquist rate will be masked
      by the sinusoids/noise just below the Nyquist rate (guaranteed only for
      up-sampling).
*/
static const struct QualityMapping quality_map[11] = {
   {  8,  4, 0.830f, 0.860f, KAISER6 }, /* Q0 */
   { 16,  4, 0.850f, 0.880f, KAISER6 }, /* Q1 */
   { 32,  4, 0.882f, 0.910f, KAISER6 }, /* Q2 */  /* 82.3% cutoff ( ~60 dB stop) 6  */
   { 48,  8, 0.895f, 0.917f, KAISER8 }, /* Q3 */  /* 84.9% cutoff ( ~80 dB stop) 8  */
   { 64,  8, 0.921f, 0.940f, KAISER8 }, /* Q4 */  /* 88.7% cutoff ( ~80 dB stop) 8  */
   { 80, 16, 0.922f, 0.940f, KAISER10}, /* Q5 */  /* 89.1% cutoff (~100 dB stop) 10 */
   { 96, 16, 0.940f, 0.945f, KAISER10}, /* Q6 */  /* 91.5% cutoff (~100 dB stop) 10 */
   {128, 16, 0.950f, 0.950f, KAISER10}, /* Q7 */  /* 93.1% cutoff (~100 dB stop) 10 */
   {160, 16, 0.960f, 0.960f, KAISER10}, /* Q8 */  /* 94.5% cutoff (~100 dB stop) 10 */
   {192, 32, 0.968f, 0.968f, KAISER12}, /* Q9 */  /* 95.5% cutoff (~100 dB stop) 10 */
   {256, 32, 0.975f, 0.975f, KAISER12}, /* Q10 */ /* 96.6% cutoff (~100 dB stop) 10 */
};
/*8,24,40,56,80,104,128,160,200,256,320*/
static double compute_func(float x, struct FuncDef *func)
{
   float y, frac;
   double interp[4];
   int ind; 
   y = x*func->oversample;
   ind = (int)floor(y);
   frac = (y-ind);
   /* CSE with handle the repeated powers */
   interp[3] =  -0.1666666667*frac + 0.1666666667*(frac*frac*frac);
   interp[2] = frac + 0.5*(frac*frac) - 0.5*(frac*frac*frac);
   /*interp[2] = 1.f - 0.5f*frac - frac*frac + 0.5f*frac*frac*frac;*/
   interp[0] = -0.3333333333*frac + 0.5*(frac*frac) - 0.1666666667*(frac*frac*frac);
   /* Just to make sure we don't have rounding problems */
   interp[1] = 1.f-interp[3]-interp[2]-interp[0];
   
   /*sum = frac*accum[1] + (1-frac)*accum[2];*/
   return interp[0]*func->table[ind] + interp[1]*func->table[ind+1] + interp[2]*func->table[ind+2] + interp[3]*func->table[ind+3];
}

/* The slow way of computing a sinc for the table. Should improve that some day */
static float sinc(float cutoff, float x, int N, struct FuncDef *window_func)
{
   /*fprintf (stderr, "%f ", x);*/
   float xx = x * cutoff;
   if (fabs(x)<1e-6)
      return cutoff;
   else if (fabs(x) > .5*N)
      return 0;
   /*FIXME: Can it really be any slower than this? */
   return cutoff*sin(M_PI*xx)/(M_PI*xx) * compute_func(fabs(2.*x/N), window_func);
}

static void cubic_coef(float frac, float interp[4])
{
   /* Compute interpolation coefficients. I'm not sure whether this corresponds to cubic interpolation
   but I know it's MMSE-optimal on a sinc */
   interp[0] =  -0.16667f*frac + 0.16667f*frac*frac*frac;
   interp[1] = frac + 0.5f*frac*frac - 0.5f*frac*frac*frac;
   /*interp[2] = 1.f - 0.5f*frac - frac*frac + 0.5f*frac*frac*frac;*/
   interp[3] = -0.33333f*frac + 0.5f*frac*frac - 0.16667f*frac*frac*frac;
   /* Just to make sure we don't have rounding problems */
   interp[2] = 1.-interp[0]-interp[1]-interp[3];
}

static int resampler_basic_direct_single(SpeexResamplerState *st, uint channel_index, const float *in, uint *in_len, float *out, uint *out_len)
{
   const int N = st->filt_len;
   int out_sample = 0;
   int last_sample = st->last_sample[channel_index];
   uint samp_frac_num = st->samp_frac_num[channel_index];
   const float *sinc_table = st->sinc_table;
   const int out_stride = st->out_stride;
   const int int_advance = st->int_advance;
   const int frac_advance = st->frac_advance;
   const uint den_rate = st->den_rate;
   float sum;

   while (!(last_sample >= (int)*in_len || out_sample >= (int)*out_len))
   {
      const float *sinc = & sinc_table[samp_frac_num*N];
      const float *iptr = & in[last_sample];

      sum = inner_product_single(sinc, iptr, N);

      out[out_stride * out_sample++] = sum;
      last_sample += int_advance;
      samp_frac_num += frac_advance;
      if (samp_frac_num >= den_rate)
      {
         samp_frac_num -= den_rate;
         last_sample++;
      }
   }

   st->last_sample[channel_index] = last_sample;
   st->samp_frac_num[channel_index] = samp_frac_num;
   return out_sample;
}

static int resampler_basic_interpolate_single(SpeexResamplerState *st, uint channel_index, const float *in, uint *in_len, float *out, uint *out_len)
{
   const int N = st->filt_len;
   int out_sample = 0;
   int last_sample = st->last_sample[channel_index];
   uint samp_frac_num = st->samp_frac_num[channel_index];
   const int out_stride = st->out_stride;
   const int int_advance = st->int_advance;
   const int frac_advance = st->frac_advance;
   const uint den_rate = st->den_rate;
   float sum;

   while (!(last_sample >= (int)*in_len || out_sample >= (int)*out_len))
   {
      const float *iptr = & in[last_sample];

      const int offset = samp_frac_num*st->oversample/st->den_rate;
      const float frac = ((float)((samp_frac_num*st->oversample) % st->den_rate))/st->den_rate;
      float interp[4];

      cubic_coef(frac, interp);
      sum = interpolate_product_single(iptr, st->sinc_table + st->oversample + 4 - offset - 2, N, st->oversample, interp);
      
      out[out_stride * out_sample++] = sum;
      last_sample += int_advance;
      samp_frac_num += frac_advance;
      if (samp_frac_num >= den_rate)
      {
         samp_frac_num -= den_rate;
         last_sample++;
      }
   }

   st->last_sample[channel_index] = last_sample;
   st->samp_frac_num[channel_index] = samp_frac_num;
   return out_sample;
}

static void update_filter(SpeexResamplerState *st)
{
   uint old_length;
   
   old_length = st->filt_len;
   st->oversample = quality_map[st->quality].oversample;
   st->filt_len = quality_map[st->quality].base_length;
   
   if (st->num_rate > st->den_rate)
   {
      /* down-sampling */
      st->cutoff = quality_map[st->quality].downsample_bandwidth * st->den_rate / st->num_rate;
      /* FIXME: divide the numerator and denominator by a certain amount if they're too large */
      st->filt_len = st->filt_len*st->num_rate / st->den_rate;
      /* Round down to make sure we have a multiple of 4 */
      st->filt_len &= (~0x3);
      if (2*st->den_rate < st->num_rate)
         st->oversample >>= 1;
      if (4*st->den_rate < st->num_rate)
         st->oversample >>= 1;
      if (8*st->den_rate < st->num_rate)
         st->oversample >>= 1;
      if (16*st->den_rate < st->num_rate)
         st->oversample >>= 1;
      if (st->oversample < 1)
         st->oversample = 1;
   } else {
      /* up-sampling */
      st->cutoff = quality_map[st->quality].upsample_bandwidth;
   }
   
   /* Choose the resampling type that requires the least amount of memory */
   if (st->den_rate <= st->oversample)
   {
      uint i;
      if (!st->sinc_table)
         st->sinc_table = (float *)malloc(st->filt_len*st->den_rate*sizeof(float));
      else if (st->sinc_table_length < st->filt_len*st->den_rate)
      {
         st->sinc_table = (float *)realloc(st->sinc_table,st->filt_len*st->den_rate*sizeof(float));
         st->sinc_table_length = st->filt_len*st->den_rate;
      }
      for (i=0;i<st->den_rate;i++)
      {
         int j;
         for (j=0;j<st->filt_len;j++)
         {
            st->sinc_table[i*st->filt_len+j] = sinc(st->cutoff,((j-(int)st->filt_len/2+1)-((float)i)/st->den_rate), st->filt_len, quality_map[st->quality].window_func);
         }
      }
      st->resampler_ptr = resampler_basic_direct_single;
      /*fprintf (stderr, "resampler uses direct sinc table and normalised cutoff %f\n", cutoff);*/
   } else {
      int i;
      if (!st->sinc_table)
         st->sinc_table = (float *)malloc((st->filt_len*st->oversample+8)*sizeof(float));
      else if (st->sinc_table_length < st->filt_len*st->oversample+8)
      {
         st->sinc_table = (float *)realloc(st->sinc_table,(st->filt_len*st->oversample+8)*sizeof(float));
         st->sinc_table_length = st->filt_len*st->oversample+8;
      }
      for (i=-4;i<(int)(st->oversample*st->filt_len+4);i++)
         st->sinc_table[i+4] = sinc(st->cutoff,(i/(float)st->oversample - st->filt_len/2), st->filt_len, quality_map[st->quality].window_func);
         st->resampler_ptr = resampler_basic_interpolate_single;
      /*fprintf (stderr, "resampler uses interpolated sinc table and normalised cutoff %f\n", cutoff);*/
   }
   st->int_advance = st->num_rate/st->den_rate;
   st->frac_advance = st->num_rate%st->den_rate;

   
   /* Here's the place where we update the filter memory to take into account
      the change in filter length. It's probably the messiest part of the code
      due to handling of lots of corner cases. */
   if (!st->mem)
   {
      uint i;
      st->mem_alloc_size = st->filt_len-1 + st->buffer_size;
      st->mem = (float*)malloc(st->nb_channels*st->mem_alloc_size * sizeof(float));
      for (i=0;i<st->nb_channels*st->mem_alloc_size;i++)
         st->mem[i] = 0;
      /*speex_warning("init filter");*/
   } else if (!st->started)
   {
      uint i;
      st->mem_alloc_size = st->filt_len-1 + st->buffer_size;
      st->mem = (float*)realloc(st->mem, st->nb_channels*st->mem_alloc_size * sizeof(float));
      for (i=0;i<st->nb_channels*st->mem_alloc_size;i++)
         st->mem[i] = 0;
      /*speex_warning("reinit filter");*/
   } else if (st->filt_len > old_length)
   {
      int i;
      /* Increase the filter length */
      /*speex_warning("increase filter size");*/
      int old_alloc_size = st->mem_alloc_size;
      if ((st->filt_len-1 + st->buffer_size) > st->mem_alloc_size)
      {
         st->mem_alloc_size = st->filt_len-1 + st->buffer_size;
         st->mem = (float*)realloc(st->mem, st->nb_channels*st->mem_alloc_size * sizeof(float));
      }
      for (i=st->nb_channels-1;i>=0;i--)
      {
         int j;
         uint olen = old_length;
         /*if (st->magic_samples[i])*/
         {
            /* Try and remove the magic samples as if nothing had happened */
            
            /* FIXME: This is wrong but for now we need it to avoid going over the array bounds */
            olen = old_length + 2*st->magic_samples[i];
            for (j=old_length-2+st->magic_samples[i];j>=0;j--)
               st->mem[i*st->mem_alloc_size+j+st->magic_samples[i]] = st->mem[i*old_alloc_size+j];
            for (j=0;j<st->magic_samples[i];j++)
               st->mem[i*st->mem_alloc_size+j] = 0;
            st->magic_samples[i] = 0;
         }
         if (st->filt_len > olen)
         {
            /* If the new filter length is still bigger than the "augmented" length */
            /* Copy data going backward */
            for (j=0;j<olen-1;j++)
               st->mem[i*st->mem_alloc_size+(st->filt_len-2-j)] = st->mem[i*st->mem_alloc_size+(olen-2-j)];
            /* Then put zeros for lack of anything better */
            for (;j<st->filt_len-1;j++)
               st->mem[i*st->mem_alloc_size+(st->filt_len-2-j)] = 0;
            /* Adjust last_sample */
            st->last_sample[i] += (st->filt_len - olen)/2;
         } else {
            /* Put back some of the magic! */
            st->magic_samples[i] = (olen - st->filt_len)/2;
            for (j=0;j<st->filt_len-1+st->magic_samples[i];j++)
               st->mem[i*st->mem_alloc_size+j] = st->mem[i*st->mem_alloc_size+j+st->magic_samples[i]];
         }
      }
   } else if (st->filt_len < old_length)
   {
      uint i;
      /* Reduce filter length, this a bit tricky. We need to store some of the memory as "magic"
         samples so they can be used directly as input the next time(s) */
      for (i=0;i<st->nb_channels;i++)
      {
         uint j;
         uint old_magic = st->magic_samples[i];
         st->magic_samples[i] = (old_length - st->filt_len)/2;
         /* We must copy some of the memory that's no longer used */
         /* Copy data going backward */
         for (j=0;j<st->filt_len-1+st->magic_samples[i]+old_magic;j++)
            st->mem[i*st->mem_alloc_size+j] = st->mem[i*st->mem_alloc_size+j+st->magic_samples[i]];
         st->magic_samples[i] += old_magic;
      }
   }

}

static int speex_resampler_set_quality(SpeexResamplerState *st, int quality)
{
   if (quality > 10 || quality < 0)
      return 1;
   if (st->quality == quality)
      return 0;
   st->quality = quality;
   if (st->initialised)
      update_filter(st);
   return 0;
}
static int speex_resampler_set_rate_frac(SpeexResamplerState *st, uint ratio_num, uint ratio_den, uint in_rate, uint out_rate)
{
   uint fact;
   uint old_den;
   uint i;
   if (st->in_rate == in_rate && st->out_rate == out_rate && st->num_rate == ratio_num && st->den_rate == ratio_den)
      return 0;

   old_den = st->den_rate;
   st->in_rate = in_rate;
   st->out_rate = out_rate;
   st->num_rate = ratio_num;
   st->den_rate = ratio_den;
   /* FIXME: This is terribly inefficient, but who cares (at least for now)? */
   uint min = st->num_rate < st->den_rate ? st->num_rate : st->den_rate;
   for (fact=2;fact<=min;fact++)
   {
      while ((st->num_rate % fact == 0) && (st->den_rate % fact == 0))
      {
         st->num_rate /= fact;
         st->den_rate /= fact;
      }
   }

   if (old_den > 0)
   {
      for (i=0;i<st->nb_channels;i++)
      {
         st->samp_frac_num[i]=st->samp_frac_num[i]*st->den_rate/old_den;
         /* Safety net */
         if (st->samp_frac_num[i] >= st->den_rate)
            st->samp_frac_num[i] = st->den_rate-1;
      }
   }

   if (st->initialised)
      update_filter(st);
   return 0;
}

SpeexResamplerState *speex_resampler_init(uint nb_channels, uint in_rate, uint out_rate, int quality, int *err)
{
   uint i;
   SpeexResamplerState *st;
   if (quality > 10 || quality < 0)
   {
      if (err)
         *err = 1;
      return NULL;
   }
   st = (SpeexResamplerState *)malloc(sizeof(SpeexResamplerState));
   st->initialised = 0;
   st->started = 0;
   st->in_rate = 0;
   st->out_rate = 0;
   st->num_rate = 0;
   st->den_rate = 0;
   st->quality = -1;
   st->sinc_table_length = 0;
   st->mem_alloc_size = 0;
   st->filt_len = 0;
   st->mem = 0;
   st->resampler_ptr = 0;
         
   st->cutoff = 1.f;
   st->nb_channels = nb_channels;
   st->in_stride = 1;
   st->out_stride = 1;
   
   st->buffer_size = 160;
   
   /* Per channel data */
   st->last_sample = (int*)malloc(nb_channels*sizeof(int));
   st->magic_samples = (uint*)malloc(nb_channels*sizeof(int));
   st->samp_frac_num = (uint*)malloc(nb_channels*sizeof(int));
   for (i=0;i<nb_channels;i++)
   {
      st->last_sample[i] = 0;
      st->magic_samples[i] = 0;
      st->samp_frac_num[i] = 0;
   }

   speex_resampler_set_quality(st, quality);
   speex_resampler_set_rate_frac(st, in_rate, out_rate, in_rate, out_rate);

   
   update_filter(st);
   
   st->initialised = 1;
   if (err)
      *err = 0;

   return st;
}

void speex_resampler_destroy(SpeexResamplerState *st)
{
   free(st->mem);
   free(st->sinc_table);
   free(st->last_sample);
   free(st->magic_samples);
   free(st->samp_frac_num);
   free(st);
}

static int speex_resampler_process_native(SpeexResamplerState *st, uint channel_index, uint *in_len, float *out, uint *out_len)
{
   int j=0;
   const int N = st->filt_len;
   int out_sample = 0;
   float *mem = st->mem + channel_index * st->mem_alloc_size;
   uint ilen;
   
   st->started = 1;
   
   /* Call the right resampler through the function ptr */
   out_sample = st->resampler_ptr(st, channel_index, mem, in_len, out, out_len);
   
   if (st->last_sample[channel_index] < (int)*in_len)
      *in_len = st->last_sample[channel_index];
   *out_len = out_sample;
   st->last_sample[channel_index] -= *in_len;
   
   ilen = *in_len;

   for(j=0;j<N-1;++j)
     mem[j] = mem[j+ilen];

   return 0;
}

static int speex_resampler_magic(SpeexResamplerState *st, uint channel_index, float **out, uint out_len) {
   uint tmp_in_len = st->magic_samples[channel_index];
   float *mem = st->mem + channel_index * st->mem_alloc_size;
   const int N = st->filt_len;
   
   speex_resampler_process_native(st, channel_index, &tmp_in_len, *out, &out_len);

   st->magic_samples[channel_index] -= tmp_in_len;
   
   /* If we couldn't process all "magic" input samples, save the rest for next time */
   if (st->magic_samples[channel_index])
   {
      uint i;
      for (i=0;i<st->magic_samples[channel_index];i++)
         mem[N-1+i]=mem[N-1+i+tmp_in_len];
   }
   *out += out_len*st->out_stride;
   return out_len;
}

static int speex_resampler_process_float(SpeexResamplerState *st, uint channel_index, const float *in, uint *in_len, float *out, uint *out_len)
{
   int j;
   uint ilen = *in_len;
   uint olen = *out_len;
   float *x = st->mem + channel_index * st->mem_alloc_size;
   const int filt_offs = st->filt_len - 1;
   const uint xlen = st->mem_alloc_size - filt_offs;
   const int istride = st->in_stride;

   if (st->magic_samples[channel_index]) 
      olen -= speex_resampler_magic(st, channel_index, &out, olen);
   if (! st->magic_samples[channel_index]) {
      while (ilen && olen) {
        uint ichunk = (ilen > xlen) ? xlen : ilen;
        uint ochunk = olen;
 
        if (in) {
           for(j=0;j<ichunk;++j)
              x[j+filt_offs]=in[j*istride];
        } else {
          for(j=0;j<ichunk;++j)
            x[j+filt_offs]=0;
        }
        speex_resampler_process_native(st, channel_index, &ichunk, out, &ochunk);
        ilen -= ichunk;
        olen -= ochunk;
        out += ochunk * st->out_stride;
        if (in)
           in += ichunk * istride;
      }
   }
   *in_len -= ilen;
   *out_len -= olen;
   return 0;
}

int speex_resampler_process_interleaved_float(SpeexResamplerState *st, const float *in, uint *in_len, float *out, uint *out_len)
{
   uint i;
   int istride_save, ostride_save;
   uint bak_len = *out_len;
   istride_save = st->in_stride;
   ostride_save = st->out_stride;
   st->in_stride = st->out_stride = st->nb_channels;
   for (i=0;i<st->nb_channels;i++)
   {
      *out_len = bak_len;
      if (in != NULL)
         speex_resampler_process_float(st, i, in+i, in_len, out+i, out_len);
      else
         speex_resampler_process_float(st, i, NULL, in_len, out+i, out_len);
   }
   st->in_stride = istride_save;
   st->out_stride = ostride_save;
   return 0;
}
