/*
 * Copyright 2003 Tungsten Graphics, inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * TUNGSTEN GRAPHICS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keithw@tungstengraphics.com>
 */

#include "main/glheader.h"
#include "main/colormac.h"
#include "main/simple_list.h"
#include "main/enums.h"

#include "vf/vf.h"

#if defined(USE_SSE_ASM)

#include "x86/rtasm/x86sse.h"
#include "x86/common_x86_asm.h"


#define X    0
#define Y    1
#define Z    2
#define W    3


struct x86_program {
   struct x86_function func;

   struct vertex_fetch *vf;
   GLboolean inputs_safe;
   GLboolean outputs_safe;
   GLboolean have_sse2;
   
   struct x86_reg identity;
   struct x86_reg chan0;
};


static struct x86_reg get_identity( struct x86_program *p )
{
   return p->identity;
}

static void emit_load4f_4( struct x86_program *p, 			   
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   sse_movups(&p->func, dest, arg0);
}

static void emit_load4f_3( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   /* Have to jump through some hoops:
    *
    * c 0 0 0
    * c 0 0 1
    * 0 0 c 1
    * a b c 1
    */
   sse_movss(&p->func, dest, x86_make_disp(arg0, 8));
   sse_shufps(&p->func, dest, get_identity(p), SHUF(X,Y,Z,W) );
   sse_shufps(&p->func, dest, dest, SHUF(Y,Z,X,W) );
   sse_movlps(&p->func, dest, arg0);
}

static void emit_load4f_2( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   /* Initialize from identity, then pull in low two words:
    */
   sse_movups(&p->func, dest, get_identity(p));
   sse_movlps(&p->func, dest, arg0);
}

static void emit_load4f_1( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   /* Pull in low word, then swizzle in identity */
   sse_movss(&p->func, dest, arg0);
   sse_shufps(&p->func, dest, get_identity(p), SHUF(X,Y,Z,W) );
}



static void emit_load3f_3( struct x86_program *p, 			   
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   /* Over-reads by 1 dword - potential SEGV if input is a vertex
    * array.
    */
   if (p->inputs_safe) {
      sse_movups(&p->func, dest, arg0);
   } 
   else {
      /* c 0 0 0
       * c c c c
       * a b c c 
       */
      sse_movss(&p->func, dest, x86_make_disp(arg0, 8));
      sse_shufps(&p->func, dest, dest, SHUF(X,X,X,X));
      sse_movlps(&p->func, dest, arg0);
   }
}

static void emit_load3f_2( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   emit_load4f_2(p, dest, arg0);
}

static void emit_load3f_1( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   emit_load4f_1(p, dest, arg0);
}

static void emit_load2f_2( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   sse_movlps(&p->func, dest, arg0);
}

static void emit_load2f_1( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   emit_load4f_1(p, dest, arg0);
}

static void emit_load1f_1( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   sse_movss(&p->func, dest, arg0);
}

static void (*load[4][4])( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 ) = {
   { emit_load1f_1, 
     emit_load1f_1, 
     emit_load1f_1, 
     emit_load1f_1 },

   { emit_load2f_1, 
     emit_load2f_2, 
     emit_load2f_2, 
     emit_load2f_2 },

   { emit_load3f_1, 
     emit_load3f_2, 
     emit_load3f_3, 
     emit_load3f_3 },

   { emit_load4f_1, 
     emit_load4f_2, 
     emit_load4f_3, 
     emit_load4f_4 } 
};

static void emit_load( struct x86_program *p,
		       struct x86_reg dest,
		       GLuint sz,
		       struct x86_reg src,
		       GLuint src_sz)
{
   load[sz-1][src_sz-1](p, dest, src);
}

static void emit_store4f( struct x86_program *p, 			   
			  struct x86_reg dest,
			  struct x86_reg arg0 )
{
   sse_movups(&p->func, dest, arg0);
}

static void emit_store3f( struct x86_program *p, 
			  struct x86_reg dest,
			  struct x86_reg arg0 )
{
   if (p->outputs_safe) {
      /* Emit the extra dword anyway.  This may hurt writecombining,
       * may cause other problems.
       */
      sse_movups(&p->func, dest, arg0);
   }
   else {
      /* Alternate strategy - emit two, shuffle, emit one.
       */
      sse_movlps(&p->func, dest, arg0);
      sse_shufps(&p->func, arg0, arg0, SHUF(Z,Z,Z,Z) ); /* NOTE! destructive */
      sse_movss(&p->func, x86_make_disp(dest,8), arg0);
   }
}

static void emit_store2f( struct x86_program *p, 
			   struct x86_reg dest,
			   struct x86_reg arg0 )
{
   sse_movlps(&p->func, dest, arg0);
}

static void emit_store1f( struct x86_program *p, 
			  struct x86_reg dest,
			  struct x86_reg arg0 )
{
   sse_movss(&p->func, dest, arg0);
}


static void (*store[4])( struct x86_program *p, 
			 struct x86_reg dest,
			 struct x86_reg arg0 ) = 
{
   emit_store1f, 
   emit_store2f, 
   emit_store3f, 
   emit_store4f 
};

static void emit_store( struct x86_program *p,
			struct x86_reg dest,
			GLuint sz,
			struct x86_reg temp )

{
   store[sz-1](p, dest, temp);
}

static void emit_pack_store_4ub( struct x86_program *p,
				 struct x86_reg dest,
				 struct x86_reg temp )
{
   /* Scale by 255.0
    */
   sse_mulps(&p->func, temp, p->chan0);

   if (p->have_sse2) {
      sse2_cvtps2dq(&p->func, temp, temp);
      sse2_packssdw(&p->func, temp, temp);
      sse2_packuswb(&p->func, temp, temp);
      sse_movss(&p->func, dest, temp);
   }
   else {
      struct x86_reg mmx0 = x86_make_reg(file_MMX, 0);
      struct x86_reg mmx1 = x86_make_reg(file_MMX, 1);
      sse_cvtps2pi(&p->func, mmx0, temp);
      sse_movhlps(&p->func, temp, temp);
      sse_cvtps2pi(&p->func, mmx1, temp);
      mmx_packssdw(&p->func, mmx0, mmx1);
      mmx_packuswb(&p->func, mmx0, mmx0);
      mmx_movd(&p->func, dest, mmx0);
   }
}

static GLint get_offset( const void *a, const void *b )
{
   return (const char *)b - (const char *)a;
}

/* Not much happens here.  Eventually use this function to try and
 * avoid saving/reloading the source pointers each vertex (if some of
 * them can fit in registers).
 */
static void get_src_ptr( struct x86_program *p,
			 struct x86_reg srcREG,
			 struct x86_reg vfREG,
			 struct vf_attr *a )
{
   struct vertex_fetch *vf = p->vf;
   struct x86_reg ptr_to_src = x86_make_disp(vfREG, get_offset(vf, &a->inputptr));

   /* Load current a[j].inputptr
    */
   x86_mov(&p->func, srcREG, ptr_to_src);
}

static void update_src_ptr( struct x86_program *p,
			 struct x86_reg srcREG,
			 struct x86_reg vfREG,
			 struct vf_attr *a )
{
   if (a->inputstride) {
      struct vertex_fetch *vf = p->vf;
      struct x86_reg ptr_to_src = x86_make_disp(vfREG, get_offset(vf, &a->inputptr));

      /* add a[j].inputstride (hardcoded value - could just as easily
       * pull the stride value from memory each time).
       */
      x86_lea(&p->func, srcREG, x86_make_disp(srcREG, a->inputstride));
      
      /* save new value of a[j].inputptr 
       */
      x86_mov(&p->func, ptr_to_src, srcREG);
   }
}


/* Lots of hardcoding
 *
 * EAX -- pointer to current output vertex
 * ECX -- pointer to current attribute 
 * 
 */
static GLboolean build_vertex_emit( struct x86_program *p )
{
   struct vertex_fetch *vf = p->vf;
   GLuint j = 0;

   struct x86_reg vertexEAX = x86_make_reg(file_REG32, reg_AX);
   struct x86_reg srcECX = x86_make_reg(file_REG32, reg_CX);
   struct x86_reg countEBP = x86_make_reg(file_REG32, reg_BP);
   struct x86_reg vfESI = x86_make_reg(file_REG32, reg_SI);
   struct x86_reg temp = x86_make_reg(file_XMM, 0);
   struct x86_reg vp0 = x86_make_reg(file_XMM, 1);
   struct x86_reg vp1 = x86_make_reg(file_XMM, 2);
   GLubyte *fixup, *label;

   /* Push a few regs?
    */
   x86_push(&p->func, countEBP);
   x86_push(&p->func, vfESI);


   /* Get vertex count, compare to zero
    */
   x86_xor(&p->func, srcECX, srcECX);
   x86_mov(&p->func, countEBP, x86_fn_arg(&p->func, 2));
   x86_cmp(&p->func, countEBP, srcECX);
   fixup = x86_jcc_forward(&p->func, cc_E);

   /* Initialize destination register. 
    */
   x86_mov(&p->func, vertexEAX, x86_fn_arg(&p->func, 3));

   /* Move argument 1 (vf) into a reg:
    */
   x86_mov(&p->func, vfESI, x86_fn_arg(&p->func, 1));

   
   /* Possibly load vp0, vp1 for viewport calcs:
    */
   if (vf->allow_viewport_emits) {
      sse_movups(&p->func, vp0, x86_make_disp(vfESI, get_offset(vf, &vf->vp[0])));
      sse_movups(&p->func, vp1, x86_make_disp(vfESI, get_offset(vf, &vf->vp[4])));
   }

   /* always load, needed or not:
    */
   sse_movups(&p->func, p->chan0, x86_make_disp(vfESI, get_offset(vf, &vf->chan_scale[0])));
   sse_movups(&p->func, p->identity, x86_make_disp(vfESI, get_offset(vf, &vf->identity[0])));

   /* Note address for loop jump */
   label = x86_get_label(&p->func);

   /* Emit code for each of the attributes.  Currently routes
    * everything through SSE registers, even when it might be more
    * efficient to stick with regular old x86.  No optimization or
    * other tricks - enough new ground to cover here just getting
    * things working.
    */
   while (j < vf->attr_count) {
      struct vf_attr *a = &vf->attr[j];
      struct x86_reg dest = x86_make_disp(vertexEAX, a->vertoffset);

      /* Now, load an XMM reg from src, perhaps transform, then save.
       * Could be shortcircuited in specific cases:
       */
      switch (a->format) {
      case EMIT_1F:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 1, x86_deref(srcECX), a->inputsize);
	 emit_store(p, dest, 1, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_2F:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 2, x86_deref(srcECX), a->inputsize);
	 emit_store(p, dest, 2, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_3F:
	 /* Potentially the worst case - hardcode 2+1 copying:
	  */
	 if (0) {
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 3, x86_deref(srcECX), a->inputsize);
	    emit_store(p, dest, 3, temp);
	    update_src_ptr(p, srcECX, vfESI, a);
	 }
	 else {
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 2, x86_deref(srcECX), a->inputsize);
	    emit_store(p, dest, 2, temp);
	    if (a->inputsize > 2) {
	       emit_load(p, temp, 1, x86_make_disp(srcECX, 8), 1);
	       emit_store(p, x86_make_disp(dest,8), 1, temp);
	    }
	    else {
	       sse_movss(&p->func, x86_make_disp(dest,8), get_identity(p));
	    }
	    update_src_ptr(p, srcECX, vfESI, a);
	 }
	 break;
      case EMIT_4F:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 emit_store(p, dest, 4, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_2F_VIEWPORT: 
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 2, x86_deref(srcECX), a->inputsize);
	 sse_mulps(&p->func, temp, vp0);
	 sse_addps(&p->func, temp, vp1);
	 emit_store(p, dest, 2, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_3F_VIEWPORT: 
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 3, x86_deref(srcECX), a->inputsize);
	 sse_mulps(&p->func, temp, vp0);
	 sse_addps(&p->func, temp, vp1);
	 emit_store(p, dest, 3, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_4F_VIEWPORT: 
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 sse_mulps(&p->func, temp, vp0);
	 sse_addps(&p->func, temp, vp1);
	 emit_store(p, dest, 4, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_3F_XYW:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 sse_shufps(&p->func, temp, temp, SHUF(X,Y,W,Z));
	 emit_store(p, dest, 3, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;

      case EMIT_1UB_1F:	 
	 /* Test for PAD3 + 1UB:
	  */
	 if (j > 0 &&
	     a[-1].vertoffset + a[-1].vertattrsize <= a->vertoffset - 3)
	 {
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 1, x86_deref(srcECX), a->inputsize);
	    sse_shufps(&p->func, temp, temp, SHUF(X,X,X,X));
	    emit_pack_store_4ub(p, x86_make_disp(dest, -3), temp); /* overkill! */
	    update_src_ptr(p, srcECX, vfESI, a);
	 }
	 else {
	    printf("Can't emit 1ub %x %x %d\n", a->vertoffset, a[-1].vertoffset, a[-1].vertattrsize );
	    return GL_FALSE;
	 }
	 break;
      case EMIT_3UB_3F_RGB:
      case EMIT_3UB_3F_BGR:
	 /* Test for 3UB + PAD1:
	  */
	 if (j == vf->attr_count - 1 ||
	     a[1].vertoffset >= a->vertoffset + 4) {
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 3, x86_deref(srcECX), a->inputsize);
	    if (a->format == EMIT_3UB_3F_BGR)
	       sse_shufps(&p->func, temp, temp, SHUF(Z,Y,X,W));
	    emit_pack_store_4ub(p, dest, temp);
	    update_src_ptr(p, srcECX, vfESI, a);
	 }
	 /* Test for 3UB + 1UB:
	  */
	 else if (j < vf->attr_count - 1 &&
		  a[1].format == EMIT_1UB_1F &&
		  a[1].vertoffset == a->vertoffset + 3) {
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 3, x86_deref(srcECX), a->inputsize);
	    update_src_ptr(p, srcECX, vfESI, a);

	    /* Make room for incoming value:
	     */
	    sse_shufps(&p->func, temp, temp, SHUF(W,X,Y,Z));

	    get_src_ptr(p, srcECX, vfESI, &a[1]);
	    emit_load(p, temp, 1, x86_deref(srcECX), a[1].inputsize);
	    update_src_ptr(p, srcECX, vfESI, &a[1]);

	    /* Rearrange and possibly do BGR conversion:
	     */
	    if (a->format == EMIT_3UB_3F_BGR)
	       sse_shufps(&p->func, temp, temp, SHUF(W,Z,Y,X));
	    else
	       sse_shufps(&p->func, temp, temp, SHUF(Y,Z,W,X));

	    emit_pack_store_4ub(p, dest, temp);
	    j++;		/* NOTE: two attrs consumed */
	 }
	 else {
	    printf("Can't emit 3ub\n");
	 }
	 return GL_FALSE;	/* add this later */
	 break;

      case EMIT_4UB_4F_RGBA:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 emit_pack_store_4ub(p, dest, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_4UB_4F_BGRA:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 sse_shufps(&p->func, temp, temp, SHUF(Z,Y,X,W));
	 emit_pack_store_4ub(p, dest, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_4UB_4F_ARGB:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 sse_shufps(&p->func, temp, temp, SHUF(W,X,Y,Z));
	 emit_pack_store_4ub(p, dest, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_4UB_4F_ABGR:
	 get_src_ptr(p, srcECX, vfESI, a);
	 emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	 sse_shufps(&p->func, temp, temp, SHUF(W,Z,Y,X));
	 emit_pack_store_4ub(p, dest, temp);
	 update_src_ptr(p, srcECX, vfESI, a);
	 break;
      case EMIT_4CHAN_4F_RGBA:
	 switch (CHAN_TYPE) {
	 case GL_UNSIGNED_BYTE:
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	    emit_pack_store_4ub(p, dest, temp);
	    update_src_ptr(p, srcECX, vfESI, a);
	    break;
	 case GL_FLOAT:
	    get_src_ptr(p, srcECX, vfESI, a);
	    emit_load(p, temp, 4, x86_deref(srcECX), a->inputsize);
	    emit_store(p, dest, 4, temp);
	    update_src_ptr(p, srcECX, vfESI, a);
	    break;
	 case GL_UNSIGNED_SHORT:
	 default:
	    printf("unknown CHAN_TYPE %s\n", _mesa_lookup_enum_by_nr(CHAN_TYPE));
	    return GL_FALSE;
	 }
	 break;
      default:
	 printf("unknown a[%d].format %d\n", j, a->format);
	 return GL_FALSE;	/* catch any new opcodes */
      }
      
      /* Increment j by at least 1 - may have been incremented above also:
       */
      j++;
   }

   /* Next vertex:
    */
   x86_lea(&p->func, vertexEAX, x86_make_disp(vertexEAX, vf->vertex_stride));

   /* decr count, loop if not zero
    */
   x86_dec(&p->func, countEBP);
   x86_test(&p->func, countEBP, countEBP); 
   x86_jcc(&p->func, cc_NZ, label);

   /* Exit mmx state?
    */
   if (p->func.need_emms)
      mmx_emms(&p->func);

   /* Land forward jump here:
    */
   x86_fixup_fwd_jump(&p->func, fixup);

   /* Pop regs and return
    */
   x86_pop(&p->func, x86_get_base_reg(vfESI));
   x86_pop(&p->func, countEBP);
   x86_ret(&p->func);

   vf->emit = (vf_emit_func)x86_get_func(&p->func);
   return GL_TRUE;
}



void vf_generate_sse_emit( struct vertex_fetch *vf )
{
   struct x86_program p;   

   if (!cpu_has_xmm) {
      vf->codegen_emit = NULL;
      return;
   }

   memset(&p, 0, sizeof(p));

   p.vf = vf;
   p.inputs_safe = 0;		/* for now */
   p.outputs_safe = 0;		/* for now */
   p.have_sse2 = cpu_has_xmm2;
   p.identity = x86_make_reg(file_XMM, 6);
   p.chan0 = x86_make_reg(file_XMM, 7);

   x86_init_func(&p.func);

   if (build_vertex_emit(&p)) {
      vf_register_fastpath( vf, GL_TRUE );
   }
   else {
      /* Note the failure so that we don't keep trying to codegen an
       * impossible state:
       */
      vf_register_fastpath( vf, GL_FALSE );
      x86_release_func(&p.func);
   }
}

#else

void vf_generate_sse_emit( struct vertex_fetch *vf )
{
   /* Dummy version for when USE_SSE_ASM not defined */
}

#endif
