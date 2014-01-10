/*
 * Copyright (C) 2000-2013 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */
#ifndef XINE_MMX_H
#define XINE_MMX_H

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#include <xine/attributes.h>

#if !defined(ATTRIBUTE_ALIGNED_MAX)
#  warning ATTRIBUTE_ALIGNED_MAX undefined. Alignment of data structures does not work !
#elif ATTRIBUTE_ALIGNED_MAX < 16
#  warning Compiler does not support proper alignment for SSE2 !
#endif

typedef	union {
	int64_t			q;	/* Quadword (64-bit) value */
	uint64_t		uq;	/* Unsigned Quadword */
	int			d[2];	/* 2 Doubleword (32-bit) values */
	unsigned int		ud[2];	/* 2 Unsigned Doubleword */
	short			w[4];	/* 4 Word (16-bit) values */
	unsigned short		uw[4];	/* 4 Unsigned Word */
	char			b[8];	/* 8 Byte (8-bit) values */
	unsigned char		ub[8];	/* 8 Unsigned Byte */
	float			s[2];	/* Single-precision (32-bit) value */
} ATTR_ALIGN(8) mmx_t;	/* On an 8-byte (64-bit) boundary */



#define	mmx_i2r(op,imm,reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "i" (imm) )

#define	mmx_m2r(op,mem,reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "m" (mem))

/* load dword from memory or gp register */
#define	mmx_a2r(op,any,reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "g" (any))

#define	mmx_r2m(op,reg,mem) \
	__asm__ __volatile__ (#op " %%" #reg ", %0" \
			      : "=m" (mem) \
			      : /* nothing */ )

#define	mmx_r2a(op,reg,any) \
	__asm__ __volatile__ (#op " %%" #reg ", %0" \
			      : "=g" (any) \
			      : /* nothing */ )

#define	mmx_r2r(op,regs,regd) \
	__asm__ __volatile__ (#op " %" #regs ", %" #regd)


#define	emms() __asm__ __volatile__ ("emms")

#define	movd_m2r(var,reg)	mmx_m2r (movd, var, reg)
#define	movd_r2m(reg,var)	mmx_r2m (movd, reg, var)
#define	movd_r2r(regs,regd)	mmx_r2r (movd, regs, regd)
#define	movd_a2r(any,reg)	mmx_a2r (movd, any, reg)
#define	movd_r2a(reg,any)	mmx_r2a (movd, reg, any)

#define	movq_m2r(var,reg)	mmx_m2r (movq, var, reg)
#define	movq_r2m(reg,var)	mmx_r2m (movq, reg, var)
#define	movq_r2r(regs,regd)	mmx_r2r (movq, regs, regd)

#define	packssdw_m2r(var,reg)	mmx_m2r (packssdw, var, reg)
#define	packssdw_r2r(regs,regd) mmx_r2r (packssdw, regs, regd)
#define	packsswb_m2r(var,reg)	mmx_m2r (packsswb, var, reg)
#define	packsswb_r2r(regs,regd) mmx_r2r (packsswb, regs, regd)

#define	packuswb_m2r(var,reg)	mmx_m2r (packuswb, var, reg)
#define	packuswb_r2r(regs,regd) mmx_r2r (packuswb, regs, regd)

#define	paddb_m2r(var,reg)	mmx_m2r (paddb, var, reg)
#define	paddb_r2r(regs,regd)	mmx_r2r (paddb, regs, regd)
#define	paddd_m2r(var,reg)	mmx_m2r (paddd, var, reg)
#define	paddd_r2r(regs,regd)	mmx_r2r (paddd, regs, regd)
#define	paddw_m2r(var,reg)	mmx_m2r (paddw, var, reg)
#define	paddw_r2r(regs,regd)	mmx_r2r (paddw, regs, regd)

#define	paddsb_m2r(var,reg)	mmx_m2r (paddsb, var, reg)
#define	paddsb_r2r(regs,regd)	mmx_r2r (paddsb, regs, regd)
#define	paddsw_m2r(var,reg)	mmx_m2r (paddsw, var, reg)
#define	paddsw_r2r(regs,regd)	mmx_r2r (paddsw, regs, regd)

#define	paddusb_m2r(var,reg)	mmx_m2r (paddusb, var, reg)
#define	paddusb_r2r(regs,regd)	mmx_r2r (paddusb, regs, regd)
#define	paddusw_m2r(var,reg)	mmx_m2r (paddusw, var, reg)
#define	paddusw_r2r(regs,regd)	mmx_r2r (paddusw, regs, regd)

#define	pand_m2r(var,reg)	mmx_m2r (pand, var, reg)
#define	pand_r2r(regs,regd)	mmx_r2r (pand, regs, regd)

#define	pandn_m2r(var,reg)	mmx_m2r (pandn, var, reg)
#define	pandn_r2r(regs,regd)	mmx_r2r (pandn, regs, regd)

#define	pcmpeqb_m2r(var,reg)	mmx_m2r (pcmpeqb, var, reg)
#define	pcmpeqb_r2r(regs,regd)	mmx_r2r (pcmpeqb, regs, regd)
#define	pcmpeqd_m2r(var,reg)	mmx_m2r (pcmpeqd, var, reg)
#define	pcmpeqd_r2r(regs,regd)	mmx_r2r (pcmpeqd, regs, regd)
#define	pcmpeqw_m2r(var,reg)	mmx_m2r (pcmpeqw, var, reg)
#define	pcmpeqw_r2r(regs,regd)	mmx_r2r (pcmpeqw, regs, regd)

#define	pcmpgtb_m2r(var,reg)	mmx_m2r (pcmpgtb, var, reg)
#define	pcmpgtb_r2r(regs,regd)	mmx_r2r (pcmpgtb, regs, regd)
#define	pcmpgtd_m2r(var,reg)	mmx_m2r (pcmpgtd, var, reg)
#define	pcmpgtd_r2r(regs,regd)	mmx_r2r (pcmpgtd, regs, regd)
#define	pcmpgtw_m2r(var,reg)	mmx_m2r (pcmpgtw, var, reg)
#define	pcmpgtw_r2r(regs,regd)	mmx_r2r (pcmpgtw, regs, regd)

#define	pmaddwd_m2r(var,reg)	mmx_m2r (pmaddwd, var, reg)
#define	pmaddwd_r2r(regs,regd)	mmx_r2r (pmaddwd, regs, regd)

#define	pmulhw_m2r(var,reg)	mmx_m2r (pmulhw, var, reg)
#define	pmulhw_r2r(regs,regd)	mmx_r2r (pmulhw, regs, regd)

#define	pmullw_m2r(var,reg)	mmx_m2r (pmullw, var, reg)
#define	pmullw_r2r(regs,regd)	mmx_r2r (pmullw, regs, regd)

#define	por_m2r(var,reg)	mmx_m2r (por, var, reg)
#define	por_r2r(regs,regd)	mmx_r2r (por, regs, regd)

#define	pslld_i2r(imm,reg)	mmx_i2r (pslld, imm, reg)
#define	pslld_m2r(var,reg)	mmx_m2r (pslld, var, reg)
#define	pslld_r2r(regs,regd)	mmx_r2r (pslld, regs, regd)
#define	psllq_i2r(imm,reg)	mmx_i2r (psllq, imm, reg)
#define	psllq_m2r(var,reg)	mmx_m2r (psllq, var, reg)
#define	psllq_r2r(regs,regd)	mmx_r2r (psllq, regs, regd)
#define	psllw_i2r(imm,reg)	mmx_i2r (psllw, imm, reg)
#define	psllw_m2r(var,reg)	mmx_m2r (psllw, var, reg)
#define	psllw_r2r(regs,regd)	mmx_r2r (psllw, regs, regd)

#define	psrad_i2r(imm,reg)	mmx_i2r (psrad, imm, reg)
#define	psrad_m2r(var,reg)	mmx_m2r (psrad, var, reg)
#define	psrad_r2r(regs,regd)	mmx_r2r (psrad, regs, regd)
#define	psraw_i2r(imm,reg)	mmx_i2r (psraw, imm, reg)
#define	psraw_m2r(var,reg)	mmx_m2r (psraw, var, reg)
#define	psraw_r2r(regs,regd)	mmx_r2r (psraw, regs, regd)

#define	psrld_i2r(imm,reg)	mmx_i2r (psrld, imm, reg)
#define	psrld_m2r(var,reg)	mmx_m2r (psrld, var, reg)
#define	psrld_r2r(regs,regd)	mmx_r2r (psrld, regs, regd)
#define	psrlq_i2r(imm,reg)	mmx_i2r (psrlq, imm, reg)
#define	psrlq_m2r(var,reg)	mmx_m2r (psrlq, var, reg)
#define	psrlq_r2r(regs,regd)	mmx_r2r (psrlq, regs, regd)
#define	psrlw_i2r(imm,reg)	mmx_i2r (psrlw, imm, reg)
#define	psrlw_m2r(var,reg)	mmx_m2r (psrlw, var, reg)
#define	psrlw_r2r(regs,regd)	mmx_r2r (psrlw, regs, regd)

#define	psubb_m2r(var,reg)	mmx_m2r (psubb, var, reg)
#define	psubb_r2r(regs,regd)	mmx_r2r (psubb, regs, regd)
#define	psubd_m2r(var,reg)	mmx_m2r (psubd, var, reg)
#define	psubd_r2r(regs,regd)	mmx_r2r (psubd, regs, regd)
#define	psubw_m2r(var,reg)	mmx_m2r (psubw, var, reg)
#define	psubw_r2r(regs,regd)	mmx_r2r (psubw, regs, regd)

#define	psubsb_m2r(var,reg)	mmx_m2r (psubsb, var, reg)
#define	psubsb_r2r(regs,regd)	mmx_r2r (psubsb, regs, regd)
#define	psubsw_m2r(var,reg)	mmx_m2r (psubsw, var, reg)
#define	psubsw_r2r(regs,regd)	mmx_r2r (psubsw, regs, regd)

#define	psubusb_m2r(var,reg)	mmx_m2r (psubusb, var, reg)
#define	psubusb_r2r(regs,regd)	mmx_r2r (psubusb, regs, regd)
#define	psubusw_m2r(var,reg)	mmx_m2r (psubusw, var, reg)
#define	psubusw_r2r(regs,regd)	mmx_r2r (psubusw, regs, regd)

#define	punpckhbw_m2r(var,reg)		mmx_m2r (punpckhbw, var, reg)
#define	punpckhbw_r2r(regs,regd)	mmx_r2r (punpckhbw, regs, regd)
#define	punpckhdq_m2r(var,reg)		mmx_m2r (punpckhdq, var, reg)
#define	punpckhdq_r2r(regs,regd)	mmx_r2r (punpckhdq, regs, regd)
#define	punpckhwd_m2r(var,reg)		mmx_m2r (punpckhwd, var, reg)
#define	punpckhwd_r2r(regs,regd)	mmx_r2r (punpckhwd, regs, regd)

#define	punpcklbw_m2r(var,reg)		mmx_m2r (punpcklbw, var, reg)
#define	punpcklbw_r2r(regs,regd)	mmx_r2r (punpcklbw, regs, regd)
#define	punpckldq_m2r(var,reg)		mmx_m2r (punpckldq, var, reg)
#define	punpckldq_r2r(regs,regd)	mmx_r2r (punpckldq, regs, regd)
#define	punpcklwd_m2r(var,reg)		mmx_m2r (punpcklwd, var, reg)
#define	punpcklwd_r2r(regs,regd)	mmx_r2r (punpcklwd, regs, regd)

#define	pxor_m2r(var,reg)	mmx_m2r (pxor, var, reg)
#define	pxor_r2r(regs,regd)	mmx_r2r (pxor, regs, regd)


/* 3DNOW extensions */

#define pavgusb_m2r(var,reg)	mmx_m2r (pavgusb, var, reg)
#define pavgusb_r2r(regs,regd)	mmx_r2r (pavgusb, regs, regd)


/* AMD MMX extensions - also available in intel SSE */


#define mmx_m2ri(op,mem,reg,imm) \
        __asm__ __volatile__ (#op " %1, %0, %%" #reg \
                              : /* nothing */ \
                              : "X" (mem), "X" (imm))
#define mmx_r2ri(op,regs,regd,imm) \
        __asm__ __volatile__ (#op " %0, %%" #regs ", %%" #regd \
                              : /* nothing */ \
                              : "X" (imm) )

#define	mmx_fetch(mem,hint) \
	__asm__ __volatile__ ("prefetch" #hint " %0" \
			      : /* nothing */ \
			      : "X" (mem))


#define	maskmovq(regs,maskreg)		mmx_r2ri (maskmovq, regs, maskreg)

#define	movntq_r2m(mmreg,var)		mmx_r2m (movntq, mmreg, var)

#define	pavgb_m2r(var,reg)		mmx_m2r (pavgb, var, reg)
#define	pavgb_r2r(regs,regd)		mmx_r2r (pavgb, regs, regd)
#define	pavgw_m2r(var,reg)		mmx_m2r (pavgw, var, reg)
#define	pavgw_r2r(regs,regd)		mmx_r2r (pavgw, regs, regd)

#define	pextrw_r2r(mmreg,reg,imm)	mmx_r2ri (pextrw, mmreg, reg, imm)

#define	pinsrw_r2r(reg,mmreg,imm)	mmx_r2ri (pinsrw, reg, mmreg, imm)

#define	pmaxsw_m2r(var,reg)		mmx_m2r (pmaxsw, var, reg)
#define	pmaxsw_r2r(regs,regd)		mmx_r2r (pmaxsw, regs, regd)

#define	pmaxub_m2r(var,reg)		mmx_m2r (pmaxub, var, reg)
#define	pmaxub_r2r(regs,regd)		mmx_r2r (pmaxub, regs, regd)

#define	pminsw_m2r(var,reg)		mmx_m2r (pminsw, var, reg)
#define	pminsw_r2r(regs,regd)		mmx_r2r (pminsw, regs, regd)

#define	pminub_m2r(var,reg)		mmx_m2r (pminub, var, reg)
#define	pminub_r2r(regs,regd)		mmx_r2r (pminub, regs, regd)

#define	pmovmskb(mmreg,reg) \
	__asm__ __volatile__ ("pmovmskb %" #mmreg ", %" #reg)
#define pmovmskb_r2a(mmreg,regvar) \
	__asm__ __volatile__ ("pmovmskb %%" #mmreg ", %0" : "=r" (regvar))

#define	pmulhuw_m2r(var,reg)		mmx_m2r (pmulhuw, var, reg)
#define	pmulhuw_r2r(regs,regd)		mmx_r2r (pmulhuw, regs, regd)

#define	prefetcht0(mem)			mmx_fetch (mem, t0)
#define	prefetcht1(mem)			mmx_fetch (mem, t1)
#define	prefetcht2(mem)			mmx_fetch (mem, t2)
#define	prefetchnta(mem)		mmx_fetch (mem, nta)

#define	psadbw_m2r(var,reg)		mmx_m2r (psadbw, var, reg)
#define	psadbw_r2r(regs,regd)		mmx_r2r (psadbw, regs, regd)

#define	pshufw_m2r(var,reg,imm)		mmx_m2ri(pshufw, var, reg, imm)
#define	pshufw_r2r(regs,regd,imm)	mmx_r2ri(pshufw, regs, regd, imm)

#define	sfence() __asm__ __volatile__ ("sfence\n\t")

typedef		union {
	int64_t                 q[2];   /* Quadword (64-bit) value */
	uint64_t                uq[2];  /* Unsigned Quadword */
	int32_t                 d[4];   /* Doubleword (32-bit) values */
	uint32_t                ud[4];  /* Unsigned Doubleword */
	short                   w[8];   /* Word (16-bit) values */
	unsigned short          uw[8];  /* Unsigned Word */
	char                    b[16];  /* Byte (8-bit) values */
	unsigned char           ub[16]; /* Unsigned Byte */
	float			sf[4];	/* Single-precision (32-bit) value */
} ATTR_ALIGN(16) sse_t;	/* On a 16 byte (128-bit) boundary */

#define FILL_SSE_UW(w) {uw:{w,w,w,w,w,w,w,w}}

#define	sse_i2r(op, imm, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "X" (imm) )

#define	sse_m2r(op, mem, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "X" (mem))

#define	sse_r2m(op, reg, mem) \
	__asm__ __volatile__ (#op " %%" #reg ", %0" \
			      : "=X" (mem) \
			      : /* nothing */ )

#define	sse_r2r(op, regs, regd) \
	__asm__ __volatile__ (#op " %" #regs ", %" #regd)

#define	sse_r2ri(op, regs, regd, imm) \
	__asm__ __volatile__ (#op " %0, %%" #regs ", %%" #regd \
			      : /* nothing */ \
			      : "X" (imm) )

#define	sse_m2ri(op, mem, reg, subop) \
	__asm__ __volatile__ (#op " %0, %%" #reg ", " #subop \
			      : /* nothing */ \
			      : "X" (mem))


#define	movaps_m2r(var, reg)	sse_m2r(movaps, var, reg)
#define	movaps_r2m(reg, var)	sse_r2m(movaps, reg, var)
#define	movaps_r2r(regs, regd)	sse_r2r(movaps, regs, regd)

#define	movntps_r2m(xmmreg, var)	sse_r2m(movntps, xmmreg, var)

#define	movups_m2r(var, reg)	sse_m2r(movups, var, reg)
#define	movups_r2m(reg, var)	sse_r2m(movups, reg, var)
#define	movups_r2r(regs, regd)	sse_r2r(movups, regs, regd)

#define	movhlps_r2r(regs, regd)	sse_r2r(movhlps, regs, regd)

#define	movlhps_r2r(regs, regd)	sse_r2r(movlhps, regs, regd)

#define	movhps_m2r(var, reg)	sse_m2r(movhps, var, reg)
#define	movhps_r2m(reg, var)	sse_r2m(movhps, reg, var)

#define	movlps_m2r(var, reg)	sse_m2r(movlps, var, reg)
#define	movlps_r2m(reg, var)	sse_r2m(movlps, reg, var)

#define	movss_m2r(var, reg)	sse_m2r(movss, var, reg)
#define	movss_r2m(reg, var)	sse_r2m(movss, reg, var)
#define	movss_r2r(regs, regd)	sse_r2r(movss, regs, regd)

#define	shufps_m2r(var, reg, index)	sse_m2ri(shufps, var, reg, index)
#define	shufps_r2r(regs, regd, index)	sse_r2ri(shufps, regs, regd, index)

#define	cvtpi2ps_m2r(var, xmmreg)	sse_m2r(cvtpi2ps, var, xmmreg)
#define	cvtpi2ps_r2r(mmreg, xmmreg)	sse_r2r(cvtpi2ps, mmreg, xmmreg)

#define	cvtps2pi_m2r(var, mmreg)	sse_m2r(cvtps2pi, var, mmreg)
#define	cvtps2pi_r2r(xmmreg, mmreg)	sse_r2r(cvtps2pi, mmreg, xmmreg)

#define	cvttps2pi_m2r(var, mmreg)	sse_m2r(cvttps2pi, var, mmreg)
#define	cvttps2pi_r2r(xmmreg, mmreg)	sse_r2r(cvttps2pi, mmreg, xmmreg)

#define	cvtsi2ss_m2r(var, xmmreg)	sse_m2r(cvtsi2ss, var, xmmreg)
#define	cvtsi2ss_r2r(reg, xmmreg)	sse_r2r(cvtsi2ss, reg, xmmreg)

#define	cvtss2si_m2r(var, reg)		sse_m2r(cvtss2si, var, reg)
#define	cvtss2si_r2r(xmmreg, reg)	sse_r2r(cvtss2si, xmmreg, reg)

#define	cvttss2si_m2r(var, reg)		sse_m2r(cvtss2si, var, reg)
#define	cvttss2si_r2r(xmmreg, reg)	sse_r2r(cvtss2si, xmmreg, reg)

#define	movmskps(xmmreg, reg) \
	__asm__ __volatile__ ("movmskps %" #xmmreg ", %" #reg)

#define	addps_m2r(var, reg)		sse_m2r(addps, var, reg)
#define	addps_r2r(regs, regd)		sse_r2r(addps, regs, regd)

#define	addss_m2r(var, reg)		sse_m2r(addss, var, reg)
#define	addss_r2r(regs, regd)		sse_r2r(addss, regs, regd)

#define	subps_m2r(var, reg)		sse_m2r(subps, var, reg)
#define	subps_r2r(regs, regd)		sse_r2r(subps, regs, regd)

#define	subss_m2r(var, reg)		sse_m2r(subss, var, reg)
#define	subss_r2r(regs, regd)		sse_r2r(subss, regs, regd)

#define	mulps_m2r(var, reg)		sse_m2r(mulps, var, reg)
#define	mulps_r2r(regs, regd)		sse_r2r(mulps, regs, regd)

#define	mulss_m2r(var, reg)		sse_m2r(mulss, var, reg)
#define	mulss_r2r(regs, regd)		sse_r2r(mulss, regs, regd)

#define	divps_m2r(var, reg)		sse_m2r(divps, var, reg)
#define	divps_r2r(regs, regd)		sse_r2r(divps, regs, regd)

#define	divss_m2r(var, reg)		sse_m2r(divss, var, reg)
#define	divss_r2r(regs, regd)		sse_r2r(divss, regs, regd)

#define	rcpps_m2r(var, reg)		sse_m2r(rcpps, var, reg)
#define	rcpps_r2r(regs, regd)		sse_r2r(rcpps, regs, regd)

#define	rcpss_m2r(var, reg)		sse_m2r(rcpss, var, reg)
#define	rcpss_r2r(regs, regd)		sse_r2r(rcpss, regs, regd)

#define	rsqrtps_m2r(var, reg)		sse_m2r(rsqrtps, var, reg)
#define	rsqrtps_r2r(regs, regd)		sse_r2r(rsqrtps, regs, regd)

#define	rsqrtss_m2r(var, reg)		sse_m2r(rsqrtss, var, reg)
#define	rsqrtss_r2r(regs, regd)		sse_r2r(rsqrtss, regs, regd)

#define	sqrtps_m2r(var, reg)		sse_m2r(sqrtps, var, reg)
#define	sqrtps_r2r(regs, regd)		sse_r2r(sqrtps, regs, regd)

#define	sqrtss_m2r(var, reg)		sse_m2r(sqrtss, var, reg)
#define	sqrtss_r2r(regs, regd)		sse_r2r(sqrtss, regs, regd)

#define	andps_m2r(var, reg)		sse_m2r(andps, var, reg)
#define	andps_r2r(regs, regd)		sse_r2r(andps, regs, regd)

#define	andnps_m2r(var, reg)		sse_m2r(andnps, var, reg)
#define	andnps_r2r(regs, regd)		sse_r2r(andnps, regs, regd)

#define	orps_m2r(var, reg)		sse_m2r(orps, var, reg)
#define	orps_r2r(regs, regd)		sse_r2r(orps, regs, regd)

#define	xorps_m2r(var, reg)		sse_m2r(xorps, var, reg)
#define	xorps_r2r(regs, regd)		sse_r2r(xorps, regs, regd)

#define	maxps_m2r(var, reg)		sse_m2r(maxps, var, reg)
#define	maxps_r2r(regs, regd)		sse_r2r(maxps, regs, regd)

#define	maxss_m2r(var, reg)		sse_m2r(maxss, var, reg)
#define	maxss_r2r(regs, regd)		sse_r2r(maxss, regs, regd)

#define	minps_m2r(var, reg)		sse_m2r(minps, var, reg)
#define	minps_r2r(regs, regd)		sse_r2r(minps, regs, regd)

#define	minss_m2r(var, reg)		sse_m2r(minss, var, reg)
#define	minss_r2r(regs, regd)		sse_r2r(minss, regs, regd)

#define	cmpps_m2r(var, reg, op)		sse_m2ri(cmpps, var, reg, op)
#define	cmpps_r2r(regs, regd, op)	sse_r2ri(cmpps, regs, regd, op)

#define	cmpeqps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 0)
#define	cmpeqps_r2r(regs, regd)		sse_r2ri(cmpps, regs, regd, 0)

#define	cmpltps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 1)
#define	cmpltps_r2r(regs, regd)		sse_r2ri(cmpps, regs, regd, 1)

#define	cmpleps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 2)
#define	cmpleps_r2r(regs, regd)		sse_r2ri(cmpps, regs, regd, 2)

#define	cmpunordps_m2r(var, reg)	sse_m2ri(cmpps, var, reg, 3)
#define	cmpunordps_r2r(regs, regd)	sse_r2ri(cmpps, regs, regd, 3)

#define	cmpneqps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 4)
#define	cmpneqps_r2r(regs, regd)	sse_r2ri(cmpps, regs, regd, 4)

#define	cmpnltps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 5)
#define	cmpnltps_r2r(regs, regd)	sse_r2ri(cmpps, regs, regd, 5)

#define	cmpnleps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 6)
#define	cmpnleps_r2r(regs, regd)	sse_r2ri(cmpps, regs, regd, 6)

#define	cmpordps_m2r(var, reg)		sse_m2ri(cmpps, var, reg, 7)
#define	cmpordps_r2r(regs, regd)	sse_r2ri(cmpps, regs, regd, 7)

#define	cmpss_m2r(var, reg, op)		sse_m2ri(cmpss, var, reg, op)
#define	cmpss_r2r(regs, regd, op)	sse_r2ri(cmpss, regs, regd, op)

#define	cmpeqss_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 0)
#define	cmpeqss_r2r(regs, regd)		sse_r2ri(cmpss, regs, regd, 0)

#define	cmpltss_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 1)
#define	cmpltss_r2r(regs, regd)		sse_r2ri(cmpss, regs, regd, 1)

#define	cmpless_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 2)
#define	cmpless_r2r(regs, regd)		sse_r2ri(cmpss, regs, regd, 2)

#define	cmpunordss_m2r(var, reg)	sse_m2ri(cmpss, var, reg, 3)
#define	cmpunordss_r2r(regs, regd)	sse_r2ri(cmpss, regs, regd, 3)

#define	cmpneqss_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 4)
#define	cmpneqss_r2r(regs, regd)	sse_r2ri(cmpss, regs, regd, 4)

#define	cmpnltss_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 5)
#define	cmpnltss_r2r(regs, regd)	sse_r2ri(cmpss, regs, regd, 5)

#define	cmpnless_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 6)
#define	cmpnless_r2r(regs, regd)	sse_r2ri(cmpss, regs, regd, 6)

#define	cmpordss_m2r(var, reg)		sse_m2ri(cmpss, var, reg, 7)
#define	cmpordss_r2r(regs, regd)	sse_r2ri(cmpss, regs, regd, 7)

#define	comiss_m2r(var, reg)		sse_m2r(comiss, var, reg)
#define	comiss_r2r(regs, regd)		sse_r2r(comiss, regs, regd)

#define	ucomiss_m2r(var, reg)		sse_m2r(ucomiss, var, reg)
#define	ucomiss_r2r(regs, regd)		sse_r2r(ucomiss, regs, regd)

#define	unpcklps_m2r(var, reg)		sse_m2r(unpcklps, var, reg)
#define	unpcklps_r2r(regs, regd)	sse_r2r(unpcklps, regs, regd)

#define	unpckhps_m2r(var, reg)		sse_m2r(unpckhps, var, reg)
#define	unpckhps_r2r(regs, regd)	sse_r2r(unpckhps, regs, regd)

#define	fxrstor(mem) \
	__asm__ __volatile__ ("fxrstor %0" \
			      : /* nothing */ \
			      : "X" (mem))

#define	fxsave(mem) \
	__asm__ __volatile__ ("fxsave %0" \
			      : /* nothing */ \
			      : "X" (mem))

#define	stmxcsr(mem) \
	__asm__ __volatile__ ("stmxcsr %0" \
			      : /* nothing */ \
			      : "X" (mem))

#define	ldmxcsr(mem) \
	__asm__ __volatile__ ("ldmxcsr %0" \
			      : /* nothing */ \
			      : "X" (mem))

/* SSE2 */

#define movdqa_m2r(var, reg)    mmx_m2r (movdqa, var, reg)
#define movdqa_r2m(reg, var)    mmx_r2m (movdqa, reg, var)
#define movdqa_r2r(regs, regd)  mmx_r2r (movdqa, regs, regd)

#define movdqu_m2r(var, reg)    mmx_m2r (movdqu, var, reg)
#define movdqu_r2m(reg, var)    mmx_r2m (movdqu, reg, var)

#define	pslldq_i2r(imm,reg)	mmx_i2r (pslldq, imm, reg)

#define	psrldq_i2r(imm,reg)	mmx_i2r (psrldq, imm, reg)

#define pshufd_m2r(var, reg, imm)   mmx_m2ri (pshufd, var, reg, imm)
#define pshufd_r2r(regs, regd, imm)   mmx_r2ri (pshufd, regs, regd, imm)

#define pshuflw_m2r(var, reg, imm)  mmx_m2ri (pshuflw, var, reg, imm)
#define pshuflw_r2r(regs, regd, imm)  mmx_r2ri (pshuflw, regs, regd, imm)

/* SSSE3 */

#define pmaddubsw_r2r(regs, regd)  mmx_r2r(pmaddubsw, regs, regd)


#endif /*ARCH_X86 */

#endif /*XINE_MMX_H*/
