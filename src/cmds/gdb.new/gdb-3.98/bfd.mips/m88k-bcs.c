/* Motorola 88000 COFF support ("Binary Compatability Standard") for BFD.
   Copyright (C) 1990-1991 Free Software Foundation, Inc.
   Written by Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* $Id: m88k-bcs.c,v 1.12 1991/07/31 16:57:52 gnu Exp $ */

#define M88 1
#include <ansidecl.h>
#include <sysdep.h>
#include "bfd.h"
#include "libbfd.h"
#include "obstack.h"
#include "m88k-bcs.h"
#include "internalcoff.h"
#include "libcoff.h"

/* Provided the symbol, returns the value reffed */
#define HOWTO_PREPARE(relocation, symbol) 	\
  {						\
  if (symbol != (asymbol *)NULL) {		\
    if (symbol->flags & BSF_FORT_COMM) {	\
      relocation = 0;				\
    }						\
    else {					\
      relocation = symbol->value;		\
    }						\
  }						\
  if (symbol->section != (asection *)NULL) {	\
    relocation += symbol->section->output_section->vma +	\
      symbol->section->output_offset;		\
  }						\
}			
 


static bfd_reloc_status_enum_type 
DEFUN(howto_hvrt16,(abfd, reloc_entry, symbol_in, data, ignore_input_section),
bfd *abfd AND
arelent *reloc_entry AND
asymbol *symbol_in AND
unsigned char *data AND
asection *ignore_input_section)
{
  long relocation;
  bfd_vma addr = reloc_entry->address;
  long x = bfd_get_16(abfd, (bfd_byte *)data + addr);

  HOWTO_PREPARE(relocation, symbol_in);

  x = (x + relocation + reloc_entry->addend) >> 16;

  bfd_put_16(abfd, x, (bfd_byte *)data + addr);
  return bfd_reloc_ok;
}



static reloc_howto_type howto_table[] = 
{
  HOWTO(R_PCR16L,02,1,16,true, 0,false,true,0,"PCR16L",false,0x0000ffff,0x0000ffff,true),
  HOWTO(R_PCR26L,02,2,16,true, 0,false,true,0,"PCR26L",false,0x03ffffff,0x03ffffff,true),
  HOWTO(R_VRT16, 00,1,16,false,0,false,true,0,"VRT16", false,0x0000ffff,0x0000ffff,true),
  HOWTO(R_HVRT16,16,1,16,false,0,false,true,howto_hvrt16,"HVRT16",false,0x0000ffff,0x0000ffff,true),
  HOWTO(R_LVRT16,00,1,16,false,0,false,true,0,"LVRT16",false,0x0000ffff,0x0000ffff,true),
  HOWTO(R_VRT32, 00,2,32,false,0,false,true,0,"VRT32", false,0xffffffff,0xffffffff,true),
};



#define BADMAG(x) MC88BADMAG(x)
#include "coffcode.h"




#define coff_write_armap bsd_write_armap


bfd_target m88k_bcs_vec =
{
  "m88kbcs",			/* name */
  bfd_target_coff_flavour_enum,
  true,				/* data byte order is big */
  true,				/* header byte order is big */

  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | DYNAMIC | WP_TEXT),

  (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
  '/',				/* ar_pad_char */
  15,				/* ar_max_namelen */
  3,				/* default alignment power */
  _do_getb64, _do_putb64,  _do_getb32, _do_putb32, _do_getb16, _do_putb16, /* data */
  _do_getb64, _do_putb64,   _do_getb32, _do_putb32, _do_getb16, _do_putb16, /* hdrs */

    {_bfd_dummy_target, coff_object_p, /* bfd_check_format */
       bfd_generic_archive_p, _bfd_dummy_target},
    {bfd_false, coff_mkobject, _bfd_generic_mkarchive, /* bfd_set_format */
       bfd_false},
    {bfd_false, coff_write_object_contents, /* bfd_write_contents */
       _bfd_write_archive_contents, bfd_false},

  JUMP_TABLE(coff),
  COFF_SWAP_TABLE
};
