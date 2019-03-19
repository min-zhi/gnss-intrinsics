/*!
 *  \file avx2_intrinsics.h
 *  \brief      Provides C functions that implement Intel's AVX2 intrinsic
 *  \details    Code style and functionality based in the VOLK project.
 *  \author    Damian Miralles
 *  \author    Jake Johnson
 *  \version   4.1a
 *  \date      Jan 23, 2018
 *  \pre       Make sure you have .bin files containing data and lookup tables
 */

#include "immintrin.h"
#include <math.h>
#include <stdio.h>

/*!
 *  \brief Generates a NCO based on the Parallel Lookup Table (PLUT) method
 *  \param[out] sig_nco Sinusoidal wave generated byt the NCO
 *  \param[in] lut Lookup table to be use for the code
 *  \param[in] blk_size Total number of elements in the sig_nco vector
 *  \param[in] rem_carr_phase Carrier phase remainder of the sinusoidal wave to
 * be generated \param[in] carr_freq Carrier frequency of the sinusoidal wave
 *  \param[in] samp_freq Sampling frequency of the signal to be generated
 */
void avx2_nco_si32(int32_t *sig_nco, const int32_t *lut, const int blk_size,
                   const double rem_carr_phase, const double carr_freq,
                   const double samp_freq) {
  int inda;
  const unsigned int eight_points = blk_size / 8;
  const unsigned int nom_carr_step =
      (unsigned int)(carr_freq * (4294967296.0 / samp_freq) + 0.5);

  // Declarations for serial implementation
  unsigned int nom_carr_phase_base =
      (unsigned int)(rem_carr_phase * (4294967296.0 / (2.0 * M_PI)) + 0.5);
  unsigned int nom_carr_idx = 0;

  // Important variable declarations
  __m256i carr_phase_base = _mm256_set1_epi32(nom_carr_phase_base);
  __m256i carr_step_base =
      _mm256_set_epi32(7 * nom_carr_step, 6 * nom_carr_step, 5 * nom_carr_step,
                       4 * nom_carr_step, 3 * nom_carr_step, 2 * nom_carr_step,
                       1 * nom_carr_step, 0 * nom_carr_step);
  __m256i carr_idx = _mm256_set1_epi32(0);
  __m256i hex_ff =
      _mm256_set_epi32(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
  __m256i nco;
  __m256i carr_step_offset = _mm256_set1_epi32(8 * nom_carr_step);

  // First iteration happens outside the loop
  carr_phase_base = _mm256_add_epi32(carr_phase_base, carr_step_base);

  for (inda = 0; inda < eight_points; inda++) {
    // Shift packed 32-bit integers in a right by imm8 while shifting in zeros
    carr_idx = _mm256_srli_epi32(carr_phase_base, 24);
    // carr_idx = _mm256_and_si256(carr_idx, hex_ff);

    // Look in lut
    nco = _mm256_i32gather_epi32(lut, carr_idx, 4);

    // Delta step
    // carr_step_base = _mm256_add_epi32(carr_step_base, carr_step_offset);
    carr_phase_base = _mm256_add_epi32(carr_phase_base, carr_step_offset);

    // 5- Store values in output buffer
    _mm256_storeu_si256((__m256i *)sig_nco, nco);

    // 6- Update pointers
    sig_nco += 8;
  }

  inda = eight_points * 8;
  nom_carr_phase_base = (unsigned int)_mm256_extract_epi32(carr_phase_base, 7);

  // generate buffer of output
  for (; inda < blk_size; ++inda) {
    // Obtain integer index in 8:24 number
    nom_carr_idx = (nom_carr_phase_base >> 24) & 0xFF;
    // Look in lut
    *sig_nco++ = lut[nom_carr_idx]; // get sample value from lut
    // Delta step
    nom_carr_phase_base += nom_carr_step;
  }
}

/*!
 *  \brief     Generates a nominal NCO based on the Direct Lookup Table (DLUT)
 * method
 *  \param[out] sig_nco Sinusoidal wave generated byt the NCO
 *  \param[in] lut Lookup table to be use for the code
 *  \param[in] blk_size Total number of elements in the sig_nco vector
 *  \param[in] rem_carr_phase Carrier phase remainder of the sinusoidal wave to
 * be generated \param[in] carr_freq Carrier frequency of the sinusoidal wave
 *  \param[in] samp_freq Sampling frequency of the signal to be generated
 */
void avx2_nom_nco_si32(int32_t *sig_nco, const int32_t *lut, const int blk_size,
                       const double rem_carr_phase, const double carr_freq,
                       const double samp_freq) {

  unsigned int carrPhaseBase =
      (rem_carr_phase * (4294967296.0 / (2.0 * M_PI)) + 0.5);
  unsigned int carrStep = (carr_freq * (4294967296.0 / samp_freq) + 0.5);
  unsigned int carrIndex = 0;
  int inda;

  // Store this values for debug purposes only
  unsigned int carrPhaseBaseVec[blk_size];
  unsigned int carrIndexVec[blk_size];

  // for each sample
  for (inda = 0; inda < blk_size; ++inda) {
    // Obtain integer index in 8:24 number
    carrIndexVec[inda] = carrIndex;
    carrIndex = (carrPhaseBase >> 24) & 0xFF;

    // Look in lut
    sig_nco[inda] = lut[carrIndex];

    // Delta step
    carrPhaseBaseVec[inda] = carrPhaseBase;
    carrPhaseBase += carrStep;
  }
}

/*!
 *  \brief     Generates an Early, late and Prompt code using SIMD Iinstructions
 *  \param[out] ecode Product vector storing the result of the multiplication
 *  \param[out] pcode Product vector storing the result of the multiplication
 *  \param[out] lcode Product vector storing the result of the multiplication
 *  \param[in] cacode Source vector with factors to multiply
 *  \param[in] blk_size Source vector with factors to multiply
 *  \param[in] rem_code_phase Number of points to Multiply in the operation
 *  \param[in] code_freq Number of points to Multiply in the operation
 *  \param[in] samp_freq Number of points to Multiply in the operation
 */
void avx2_nom_code_si32(int32_t *ecode, int32_t *pcode, int32_t *lcode,
                        const int32_t *cacode, const int blk_size,
                        const double rem_code_phase, const double code_freq,
                        const double samp_freq) {

  int inda;
  double earlyLateSpc = 0.5;
  double codePhaseStep = code_freq / samp_freq;
  double baseCode;
  int pCodeIdx, eCodeIdx, lCodeIdx;

  // for each sample
  for (inda = 0; inda < blk_size; ++inda) {
    baseCode = (inda * codePhaseStep + rem_code_phase);
    pCodeIdx = (int32_t)(baseCode) < baseCode ? (baseCode + 1) : baseCode;
    eCodeIdx = (int32_t)(baseCode - earlyLateSpc) < (baseCode - earlyLateSpc)
                   ? (baseCode - earlyLateSpc + 1)
                   : (baseCode - earlyLateSpc);
    lCodeIdx = (int32_t)(baseCode + earlyLateSpc) < (baseCode + earlyLateSpc)
                   ? (baseCode + earlyLateSpc + 1)
                   : (baseCode + earlyLateSpc);

    ecode[inda] = *(cacode + eCodeIdx);
    pcode[inda] = *(cacode + pCodeIdx);
    lcode[inda] = *(cacode + lCodeIdx);
  }
}

/*!
 *  \brief     Generates an Early, late and Prompt code using SIMD Iinstructions
 *  \param[out] ecode Product vector storing the result of the multiplication
 *  \param[out] pcode Product vector storing the result of the multiplication
 *  \param[out] lcode Product vector storing the result of the multiplication
 *  \param[in] cacode Source vector with factors to multiply
 *  \param[in] blk_size Source vector with factors to multiply
 *  \param[in] rem_code_phase Number of points to Multiply in the operation
 *  \param[in] code_freq Number of points to Multiply in the operation
 *  \param[in] samp_freq Number of points to Multiply in the operation
 */
void avx2_code_si32(int32_t *ecode, int32_t *pcode, int32_t *lcode,
                    const int32_t *cacode, const int blk_size,
                    const float rem_code_phase, const float code_freq,
                    const float samp_freq) {

  int inda;
  const unsigned int eight_points = blk_size / 8;
  float earlyLateSpc = 0.5;
  float codePhaseStep = code_freq / samp_freq;
  float baseCode;
  int pCodeIdx, eCodeIdx, lCodeIdx;

  // Important variable declarations
  __m256 ecode_phase_base = _mm256_set1_ps(rem_code_phase - earlyLateSpc + 0.5);
  __m256 pcode_phase_base = _mm256_set1_ps(rem_code_phase + 0.5);
  __m256 lcode_phase_base = _mm256_set1_ps(rem_code_phase + earlyLateSpc + 0.5);

  __m256 code_step_base =
      _mm256_set_ps(7 * codePhaseStep, 6 * codePhaseStep, 5 * codePhaseStep,
                    4 * codePhaseStep, 3 * codePhaseStep, 2 * codePhaseStep,
                    1 * codePhaseStep, 0 * codePhaseStep);
  __m256i ecode_idx = _mm256_set1_epi32(0);
  __m256i pcode_idx = _mm256_set1_epi32(0);
  __m256i lcode_idx = _mm256_set1_epi32(0);

  __m256i elut, plut, llut;
  __m256 code_step_offset = _mm256_set1_ps(8 * codePhaseStep);

  // First iteration happens outside the loop
  ecode_phase_base = _mm256_add_ps(ecode_phase_base, code_step_base);
  pcode_phase_base = _mm256_add_ps(pcode_phase_base, code_step_base);
  lcode_phase_base = _mm256_add_ps(lcode_phase_base, code_step_base);

  for (inda = 0; inda < eight_points; inda++) {
    // Obtain integer index in 8:24 number
    ecode_idx = _mm256_cvtps_epi32(ecode_phase_base);
    pcode_idx = _mm256_cvtps_epi32(pcode_phase_base);
    lcode_idx = _mm256_cvtps_epi32(lcode_phase_base);

    // Look in lut
    elut = _mm256_i32gather_epi32(cacode, ecode_idx, 4);
    plut = _mm256_i32gather_epi32(cacode, pcode_idx, 4);
    llut = _mm256_i32gather_epi32(cacode, lcode_idx, 4);

    // Delta step
    ecode_phase_base = _mm256_add_ps(ecode_phase_base, code_step_offset);
    pcode_phase_base = _mm256_add_ps(pcode_phase_base, code_step_offset);
    lcode_phase_base = _mm256_add_ps(lcode_phase_base, code_step_offset);

    // 5- Store values in output buffer
    _mm256_storeu_si256((__m256i *)ecode, elut);
    _mm256_storeu_si256((__m256i *)pcode, plut);
    _mm256_storeu_si256((__m256i *)lcode, llut);

    // 6- Update pointers
    ecode += 8;
    pcode += 8;
    lcode += 8;
  }

  inda = eight_points * 8;

  // generate buffer of output
  for (; inda < blk_size; ++inda) {
    baseCode = (inda * codePhaseStep + rem_code_phase);
    pCodeIdx = (int32_t)(baseCode) < baseCode ? (baseCode + 1) : baseCode;
    eCodeIdx = (int32_t)(baseCode - earlyLateSpc) < (baseCode - earlyLateSpc)
                   ? (baseCode - earlyLateSpc + 1)
                   : (baseCode - earlyLateSpc);
    lCodeIdx = (int32_t)(baseCode + earlyLateSpc) < (baseCode + earlyLateSpc)
                   ? (baseCode + earlyLateSpc + 1)
                   : (baseCode + earlyLateSpc);

    ecode[inda] = *(cacode + eCodeIdx);
    pcode[inda] = *(cacode + pCodeIdx);
    lcode[inda] = *(cacode + lCodeIdx);
  }
}

/*!
 *  \brief Generates a NCO based on the Direct Lookup Table (DLUT) method.
 *  \param[out] sig_nco Sinusoidal wave generated byt the NCO
 *  \param[in] lut Lookup table to be use for the code
 *  \param[in] blk_size Total number of elements in the sig_nco vector
 *  \param[in] rem_carr_phase Carrier phase remainder of the sinusoidal wave to
 * be generated \param[in] carr_freq Carrier frequency of the sinusoidal wave
 *  \param[in] samp_freq Sampling frequency of the signal to be generated
 */
void avx2_nco_fl32(float *sig_nco, const float *lut, const int blk_size,
                   const double rem_carr_phase, const double carr_freq,
                   const double samp_freq) {
  int inda;
  const unsigned int eight_points = blk_size / 8;
  const unsigned int nom_carr_step =
      (unsigned int)(carr_freq * (4294967296.0 / samp_freq) + 0.5);

  // Declarations for serial implementation
  unsigned int nom_carr_phase_base =
      (unsigned int)(rem_carr_phase * (4294967296.0 / (2.0 * M_PI)) + 0.5);
  unsigned int nom_carr_idx = 0;

  // Important variable declarations
  __m256i carr_phase_base = _mm256_set1_epi32(nom_carr_phase_base);
  __m256i carr_step_base =
      _mm256_set_epi32(7 * nom_carr_step, 6 * nom_carr_step, 5 * nom_carr_step,
                       4 * nom_carr_step, 3 * nom_carr_step, 2 * nom_carr_step,
                       1 * nom_carr_step, 0 * nom_carr_step);
  __m256i carr_idx = _mm256_set1_epi32(0);
  __m256i hex_ff =
      _mm256_set_epi32(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
  __m256 nco;
  __m256i carr_step_offset = _mm256_set1_epi32(8 * nom_carr_step);

  // First iteration happens outside the loop
  carr_phase_base = _mm256_add_epi32(carr_phase_base, carr_step_base);

  for (inda = 0; inda < eight_points; inda++) {
    // Obtain integer index in 8:24 number
    carr_idx = _mm256_srli_epi32(carr_phase_base, 24);
    carr_idx = _mm256_and_si256(carr_idx, hex_ff);

    // Look in lut
    nco = _mm256_i32gather_ps(lut, carr_idx, 4);

    // Delta step
    // carr_step_base = _mm256_add_epi32(carr_step_base, carr_step_offset);
    carr_phase_base = _mm256_add_epi32(carr_phase_base, carr_step_offset);

    // 5- Store values in output buffer
    _mm256_storeu_ps((float *)sig_nco, nco);

    // 6- Update pointers
    sig_nco += 8;
  }

  inda = eight_points * 8;
  nom_carr_phase_base = (unsigned int)_mm256_extract_epi32(carr_phase_base, 7);

  // generate buffer of output
  for (; inda < blk_size; ++inda) {
    // Obtain integer index in 8:24 number
    nom_carr_idx = (nom_carr_phase_base >> 24) & 0xFF;
    // Look in lut
    *sig_nco++ = lut[nom_carr_idx]; // get sample value from lut
    // Delta step
    nom_carr_phase_base += nom_carr_step;
  }
}

/*!
 *  \brief Generates a nominal NCO based on the direct lookup table approach
 *  \param[out] sig_nco Sinusoidal wave generated byt the NCO
 *  \param[in] lut Lookup table to be use for the code
 *  \param[in] blk_size Total number of elements in the sig_nco vector
 *  \param[in] rem_carr_phase Carrier phase remainder of the sinusoidal wave to
 * be generated \param[in] carr_freq Carrier frequency of the sinusoidal wave
 *  \param[in] samp_freq Sampling frequency of the signal to be generated
 */
void avx2_nom_nco_fl32(float *sig_nco, const float *lut, const int blk_size,
                       const double rem_carr_phase, const double carr_freq,
                       const double samp_freq) {

  unsigned int carrPhaseBase =
      (rem_carr_phase * (4294967296.0 / (2.0 * M_PI)) + 0.5);
  unsigned int carrStep = (carr_freq * (4294967296.0 / samp_freq) + 0.5);
  unsigned int carrIndex = 0;
  int inda;

  // Store this values for debug purposes only
  unsigned int carrPhaseBaseVec[blk_size];
  unsigned int carrIndexVec[blk_size];

  // for each sample
  for (inda = 0; inda < blk_size; ++inda) {
    // Obtain integer index in 8:24 number
    carrIndexVec[inda] = carrIndex;
    carrIndex = (carrPhaseBase >> 24) & 0xFF;

    // Look in lut
    sig_nco[inda] = lut[carrIndex];

    // Delta step
    carrPhaseBaseVec[inda] = carrPhaseBase;
    carrPhaseBase += carrStep;
  }
}

/*!
 *  \brief Generates an Early, Late and Propmt CA code based on the direct
 * lookup table approach \param[out] ecode Early CA code \param[out] pcode
 * Prompt CA code \param[out] lcode Late CA code \param[in] cacode Nominal
 * satellite ranging code \param[in] blk_size Total number of elements in the
 * sig_nco vector \param[in] rem_code_phase Carrier phase remainder of the
 * sinusoidal wave to be generated \param[in] code_freq Carrier frequency of the
 * sinusoidal wave \param[in] samp_freq Sampling frequency of the signal to be
 * generated
 */
void avx2_nom_code_fl32(float *ecode, float *pcode, float *lcode,
                        const float *cacode, const int blk_size,
                        const double rem_code_phase, const double code_freq,
                        const double samp_freq) {

  int inda;
  double earlyLateSpc = 0.5;
  double codePhaseStep = code_freq / samp_freq;
  double baseCode;
  int pCodeIdx, eCodeIdx, lCodeIdx;

  // for each sample
  for (inda = 0; inda < blk_size; ++inda) {
    baseCode = (inda * codePhaseStep + rem_code_phase);
    pCodeIdx = (float)(baseCode) < baseCode ? (baseCode + 1) : baseCode;
    eCodeIdx = (float)(baseCode - earlyLateSpc) < (baseCode - earlyLateSpc)
                   ? (baseCode - earlyLateSpc + 1)
                   : (baseCode - earlyLateSpc);
    lCodeIdx = (float)(baseCode + earlyLateSpc) < (baseCode + earlyLateSpc)
                   ? (baseCode + earlyLateSpc + 1)
                   : (baseCode + earlyLateSpc);

    ecode[inda] = *(cacode + eCodeIdx);
    pcode[inda] = *(cacode + pCodeIdx);
    lcode[inda] = *(cacode + lCodeIdx);
  }
}

/*!
 *  \brief Generates an Early, Late and Propmt CA code based on the Parallelized
 * Lookup Table (PLUT) approach.
 *  \param[out] ecode Early CA code
 *  \param[out] pcode Prompt CA code
 *  \param[out] lcode Late CA code
 *  \param[in] cacode Nominal satellite ranging code
 *  \param[in] blk_size Total number of elements in the sig_nco vector
 *  \param[in] rem_code_phase Carrier phase remainder of the sinusoidal wave
 *  \param[in] code_freq Carrier frequency of the sinusoidal wave
 *  \param[in] samp_freq Sampling frequency of the signal to be generated
 */
void avx2_code_fl32(float *ecode, float *pcode, float *lcode,
                    const float *cacode, const int blk_size,
                    const float rem_code_phase, const float code_freq,
                    const float samp_freq) {

  int inda;
  const unsigned int eight_points = blk_size / 8;
  float earlyLateSpc = 0.5;
  float codePhaseStep = code_freq / samp_freq;
  float baseCode;
  int pCodeIdx, eCodeIdx, lCodeIdx;

  // Important variable declarations
  __m256 ecode_phase_base = _mm256_set1_ps(rem_code_phase - earlyLateSpc + 0.5);
  __m256 pcode_phase_base = _mm256_set1_ps(rem_code_phase + 0.5);
  __m256 lcode_phase_base = _mm256_set1_ps(rem_code_phase + earlyLateSpc + 0.5);

  __m256 code_step_base =
      _mm256_set_ps(7 * codePhaseStep, 6 * codePhaseStep, 5 * codePhaseStep,
                    4 * codePhaseStep, 3 * codePhaseStep, 2 * codePhaseStep,
                    1 * codePhaseStep, 0 * codePhaseStep);
  __m256i ecode_idx = _mm256_set1_epi32(0);
  __m256i pcode_idx = _mm256_set1_epi32(0);
  __m256i lcode_idx = _mm256_set1_epi32(0);

  __m256 elut, plut, llut;
  __m256 code_step_offset = _mm256_set1_ps(8 * codePhaseStep);

  // First iteration happens outside the loop
  ecode_phase_base = _mm256_add_ps(ecode_phase_base, code_step_base);
  pcode_phase_base = _mm256_add_ps(pcode_phase_base, code_step_base);
  lcode_phase_base = _mm256_add_ps(lcode_phase_base, code_step_base);

  for (inda = 0; inda < eight_points; inda++) {
    // Obtain integer index in 8:24 number
    ecode_idx = _mm256_cvtps_epi32(ecode_phase_base);
    pcode_idx = _mm256_cvtps_epi32(pcode_phase_base);
    lcode_idx = _mm256_cvtps_epi32(lcode_phase_base);

    // Look in lut
    elut = _mm256_i32gather_ps(cacode, ecode_idx, 4);
    plut = _mm256_i32gather_ps(cacode, pcode_idx, 4);
    llut = _mm256_i32gather_ps(cacode, lcode_idx, 4);

    // Delta step
    ecode_phase_base = _mm256_add_ps(ecode_phase_base, code_step_offset);
    pcode_phase_base = _mm256_add_ps(pcode_phase_base, code_step_offset);
    lcode_phase_base = _mm256_add_ps(lcode_phase_base, code_step_offset);

    // 5- Store values in output buffer
    _mm256_storeu_ps((float *)ecode, elut);
    _mm256_storeu_ps((float *)pcode, plut);
    _mm256_storeu_ps((float *)lcode, llut);

    // 6- Update pointers
    ecode += 8;
    pcode += 8;
    lcode += 8;
  }

  inda = eight_points * 8;

  // generate buffer of output
  for (; inda < blk_size; ++inda) {
    baseCode = (inda * codePhaseStep + rem_code_phase);
    pCodeIdx = (float)(baseCode) < baseCode ? (baseCode + 1) : baseCode;
    eCodeIdx = (float)(baseCode - earlyLateSpc) < (baseCode - earlyLateSpc)
                   ? (baseCode - earlyLateSpc + 1)
                   : (baseCode - earlyLateSpc);
    lCodeIdx = (float)(baseCode + earlyLateSpc) < (baseCode + earlyLateSpc)
                   ? (baseCode + earlyLateSpc + 1)
                   : (baseCode + earlyLateSpc);

    ecode[inda] = *(cacode + eCodeIdx);
    pcode[inda] = *(cacode + pCodeIdx);
    lcode[inda] = *(cacode + lCodeIdx);
  }
}

/*!
 *  \brief Multiply and accumulates product of two vectors storing the result in
 * a fl32 type
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each vector
 */
static inline float avx2_mul_and_acc_fl32(const float *aVector,
                                          const float *bVector,
                                          unsigned int num_points) {

  float returnValue = 0;
  unsigned int number = 0;
  const unsigned int eigthPoints = num_points / 8;

  const float *aPtr = aVector;
  const float *bPtr = bVector;
  float tempBuffer[8];

  __m256 aVal, bVal, cVal;
  __m256 accumulator = _mm256_setzero_ps();

  for (; number < eigthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_ps((float *)aPtr);
    bVal = _mm256_loadu_ps((float *)bPtr);

    // TODO: More efficient way to exclude having this intermediate cVal
    // variable??
    cVal = _mm256_mul_ps(aVal, bVal);

    // accumulator += _mm256_mullo_epi16(aVal, bVal);
    accumulator = _mm256_add_ps(accumulator, cVal);

    // Increment pointers
    aPtr += 8;
    bPtr += 8;
  }

  _mm256_storeu_ps((float *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];

  // Perform non SIMD leftover operations
  number = eigthPoints * 8;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++) * (*bPtr++);
  }
  return returnValue;
}

/*!
 *  \brief Multiply point to point two vectors together as a fl32 type
 * \param[out] cvector Product of point to point multiplication
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each
 * vector
 */
static inline void avx2_fl32_x2_mul_fl32(float *cVector, const float *aVector,
                                         const float *bVector,
                                         unsigned int num_points) {

  unsigned int number = 0;
  const unsigned int eigthPoints = num_points / 8;

  float *cPtr = cVector;
  const float *aPtr = aVector;
  const float *bPtr = bVector;

  __m256 aVal, bVal, cVal;

  for (; number < eigthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_ps((float *)aPtr);
    bVal = _mm256_loadu_ps((float *)bPtr);

    // Multiply packed 16-bit integers in a and b, producing intermediate
    // signed 32-bit integers. Truncate each intermediate integer to the 18
    // most significant bits, round by adding 1, and store bits [16:1] to dst.
    cVal = _mm256_mul_ps(aVal, bVal);

    // Store 256-bits of integer data from a into memory. mem_addr does
    // not need to be aligned on any particular boundary.
    _mm256_storeu_ps((float *)cPtr, cVal);

    // Increment pointers
    aPtr += 8;
    bPtr += 8;
    cPtr += 8;
  }
}

/*!
 *  \brief Multiply and accumulates product of two vectors storing the result in
 * a si32 type
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each vector
 */
static inline double avx2_mul_and_acc_si32(const int *aVector,
                                           const int *bVector,
                                           unsigned int num_points) {

  int returnValue = 0;
  unsigned int number = 0;
  const unsigned int eigthPoints = num_points / 8;

  const int *aPtr = aVector;
  const int *bPtr = bVector;
  int tempBuffer[8];

  __m256i aVal, bVal, cVal;
  __m256i accumulator = _mm256_setzero_si256();

  for (; number < eigthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    bVal = _mm256_loadu_si256((__m256i *)bPtr);

    // TODO: More efficient way to exclude having this intermediate cVal
    // variable??
    cVal = _mm256_mullo_epi32(aVal, bVal);

    // accumulator += _mm256_mullo_epi16(aVal, bVal);
    accumulator = _mm256_add_epi32(accumulator, cVal);

    // Increment pointers
    aPtr += 8;
    bPtr += 8;
  }

  _mm256_storeu_si256((__m256i *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];

  // Perform non SIMD leftover operations
  number = eigthPoints * 8;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++) * (*bPtr++);
  }
  return returnValue;
}

/*!
 *  \brief Multiply point to point two vectors together as a si32 type
 * \param[out] cvector Product of point to point multiplication
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each
 * vector
 */
static inline void avx2_si32_x2_mul_si32(int *cVector, const int *aVector,
                                         const int *bVector,
                                         unsigned int num_points) {

  unsigned int number = 0;
  const unsigned int eigthPoints = num_points / 8;

  int *cPtr = cVector;
  const int *aPtr = aVector;
  const int *bPtr = bVector;

  __m256i aVal, bVal, cVal;

  for (; number < eigthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    bVal = _mm256_loadu_si256((__m256i *)bPtr);

    // Multiply packed 16-bit integers in a and b, producing intermediate
    // signed 32-bit integers. Truncate each intermediate integer to the 18
    // most significant bits, round by adding 1, and store bits [16:1] to dst.
    cVal = _mm256_mullo_epi32(aVal, bVal);

    // Store 256-bits of integer data from a into memory. mem_addr does
    // not need to be aligned on any particular boundary.
    _mm256_storeu_si256((__m256i *)cPtr, cVal);

    // Increment pointers
    aPtr += 8;
    bPtr += 8;
    cPtr += 8;
  }

  number = eigthPoints * 8;
  for (; number < num_points; number++) {
    *cPtr++ = (*aPtr++) * (*bPtr++);
  }
}

/*!
 *  \brief Multiply the elements of a vector
 * \param[in] inputBuffer Elements stored in dedicated vector
 * \param[in] num_points Number of points in each vector
 */
static inline void avx2_mul_short(short *cVector, const short *aVector,
                                  const short *bVector,
                                  unsigned int num_points) {

  unsigned int number = 0;
  const unsigned int sixteenthPoints = num_points / 16;

  short *cPtr = cVector;
  const short *aPtr = aVector;
  const short *bPtr = bVector;

  __m256i aVal, bVal, cVal;

  for (; number < sixteenthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    bVal = _mm256_loadu_si256((__m256i *)bPtr);

    // Multiply packed 16-bit integers in a and b, producing intermediate
    // signed 32-bit integers. Truncate each intermediate integer to the 18
    // most significant bits, round by adding 1, and store bits [16:1] to dst.
    cVal = _mm256_mullo_epi16(aVal, bVal);

    // Store 256-bits of integer data from a into memory. mem_addr does
    // not need to be aligned on any particular boundary.
    _mm256_storeu_si256((__m256i *)cPtr, cVal);

    // Increment pointers
    aPtr += 16;
    bPtr += 16;
    cPtr += 16;
  }

  // Perform non SIMD leftover operations
  number = sixteenthPoints * 16;
  for (; number < num_points; number++) {
    *cPtr++ = (*aPtr++) * (*bPtr++);
  }
}

/*!
 *  \brief Accumulate the elements of a vector
 * \param[in] inputBuffer Elements stored in dedicated vector
 * \param[in] num_points Number of points in each vector
 */
static inline double avx_accumulate_short(const short *inputBuffer,
                                          unsigned int num_points) {
  int returnValue = 0;
  unsigned int number = 0;
  const unsigned int sixteenthPoints = num_points / 16;

  const short *aPtr = inputBuffer;
  /*__VOLK_ATTR_ALIGNED(32)*/ short tempBuffer[16];

  __m256i accumulator = _mm256_setzero_si256();
  __m256i aVal = _mm256_setzero_si256();

  for (; number < sixteenthPoints; number++) {
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    accumulator = _mm256_adds_epi16(accumulator, aVal);
    aPtr += 16;
  }

  _mm256_storeu_si256((__m256i *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];
  returnValue += tempBuffer[8];
  returnValue += tempBuffer[9];
  returnValue += tempBuffer[10];
  returnValue += tempBuffer[11];
  returnValue += tempBuffer[12];
  returnValue += tempBuffer[13];
  returnValue += tempBuffer[14];
  returnValue += tempBuffer[15];

  number = sixteenthPoints * 16;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++);
  }
  return returnValue;
}

/*!
 * \brief Accumulate the elements of a vector using unsaturated math
 * \details Using the unsaturated math, once the maximum value in the data type
 * is reached, the value will rollover and start from its minimum value.
 * \param[in] inputBuffer Elements stored in dedicated vector
 * \param[in] num_points Number of points in each vector
 */
static inline double avx_accumulate_short_unsat(const short *inputBuffer,
                                                unsigned int num_points) {
  int returnValue = 0;
  unsigned int number = 0;
  const unsigned int sixteenthPoints = num_points / 16;

  const short *aPtr = inputBuffer;
  /*__VOLK_ATTR_ALIGNED(32)*/ short tempBuffer[16];

  __m256i accumulator = _mm256_setzero_si256();
  __m256i aVal = _mm256_setzero_si256();

  for (; number < sixteenthPoints; number++) {
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    accumulator = _mm256_adds_epi16(accumulator, aVal);
    aPtr += 16;
  }

  _mm256_storeu_si256((__m256i *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];
  returnValue += tempBuffer[8];
  returnValue += tempBuffer[9];
  returnValue += tempBuffer[10];
  returnValue += tempBuffer[11];
  returnValue += tempBuffer[12];
  returnValue += tempBuffer[13];
  returnValue += tempBuffer[14];
  returnValue += tempBuffer[15];

  number = sixteenthPoints * 16;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++);
  }
  return returnValue;
}

/*!
 *  \brief Multiply and accumulates product of two vectors storing the result in
 * a short type
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each vector
 */
static inline double avx2_mul_and_acc_short(const short *aVector,
                                            const short *bVector,
                                            unsigned int num_points) {

  int returnValue = 0;
  unsigned int number = 0;
  const unsigned int sixteenthPoints = num_points / 16;

  const short *aPtr = aVector;
  const short *bPtr = bVector;
  short tempBuffer[16];

  __m256i aVal, bVal, cVal;
  __m256i accumulator = _mm256_setzero_si256();

  for (; number < sixteenthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    bVal = _mm256_loadu_si256((__m256i *)bPtr);

    // TODO: More efficient way to exclude having this intermediate cVal
    // variable??
    cVal = _mm256_mullo_epi16(aVal, bVal);

    // accumulator += _mm256_mullo_epi16(aVal, bVal);
    accumulator = _mm256_adds_epi16(accumulator, cVal);

    // Increment pointers
    aPtr += 16;
    bPtr += 16;
  }

  _mm256_storeu_si256((__m256i *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];
  returnValue += tempBuffer[8];
  returnValue += tempBuffer[9];
  returnValue += tempBuffer[10];
  returnValue += tempBuffer[11];
  returnValue += tempBuffer[12];
  returnValue += tempBuffer[13];
  returnValue += tempBuffer[14];
  returnValue += tempBuffer[15];

  // Perform non SIMD leftover operations
  number = sixteenthPoints * 16;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++) * (*bPtr++);
  }
  return returnValue;
}

/*!
 *  \brief Multiply point to point two vectors together as a si32 type
 * \param[out] cvector Product of point to point multiplication
 * \param[in] avector First vector to multiply
 * \param[in] bvector Second vector to multiply
 * \param[in] num_points Number of points in each
 * vector
 */
static inline void avx2_mul_short_store_int(short *cVector,
                                            const short *aVector,
                                            const short *bVector,
                                            unsigned int num_points) {

  unsigned int number = 0;
  const unsigned int sixteenthPoints = num_points / 16;

  short *cPtr = cVector;
  const short *aPtr = aVector;
  const short *bPtr = bVector;

  __m256i aVal, bVal, cVal;

  for (; number < sixteenthPoints; number++) {

    // Load 256-bits of integer data from memory into dst. mem_addr does not
    // need to be aligned on any particular boundary.
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    bVal = _mm256_loadu_si256((__m256i *)bPtr);

    // Multiply packed 16-bit integers in a and b, producing intermediate
    // signed 32-bit integers. Truncate each intermediate integer to the 18
    // most significant bits, round by adding 1, and store bits [16:1] to dst.
    cVal = _mm256_mullo_epi16(aVal, bVal);

    // Store 256-bits of integer data from a into memory. mem_addr does
    // not need to be aligned on any particular boundary.
    _mm256_storeu_si256((__m256i *)cPtr, cVal);

    // Increment pointers
    aPtr += 16;
    bPtr += 16;
    cPtr += 16;
  }

  // Perform non SIMD leftover operations
  number = sixteenthPoints * 16;
  for (; number < num_points; number++) {
    *cPtr++ = (*aPtr++) * (*bPtr++);
  }
}

/*!
 *  \brief Accumulate the elements of a vector
 * \param[in] inputBuffer Elements stored in dedicated vector
 * \param[in] num_points Number of points in each vector
 */
static inline double avx_accumulate_int(const int *inputBuffer,
                                        unsigned int num_points) {
  int returnValue = 0;
  unsigned int number = 0;
  const unsigned int eighthPoints = num_points / 8;

  const int *aPtr = inputBuffer;
  /*__VOLK_ATTR_ALIGNED(32)*/ int tempBuffer[8];

  __m256i accumulator = _mm256_setzero_si256();
  __m256i aVal = _mm256_setzero_si256();

  // no saturation used
  for (; number < eighthPoints; number++) {
    aVal = _mm256_loadu_si256((__m256i *)aPtr);
    accumulator = _mm256_add_epi32(accumulator, aVal);
    aPtr += 8;
  }

  _mm256_storeu_si256((__m256i *)tempBuffer, accumulator);

  returnValue = tempBuffer[0];
  returnValue += tempBuffer[1];
  returnValue += tempBuffer[2];
  returnValue += tempBuffer[3];
  returnValue += tempBuffer[4];
  returnValue += tempBuffer[5];
  returnValue += tempBuffer[6];
  returnValue += tempBuffer[7];

  number = eighthPoints * 8;
  for (; number < num_points; number++) {
    returnValue += (*aPtr++);
  }
  return returnValue;
}
