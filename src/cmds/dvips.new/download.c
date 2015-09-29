/*
 *   Code to download a font definition at the beginning of a section.
 */
#include "structures.h" /* The copyright notice in that file is included too! */
/*
 *   These are the external routines we call.
 */
extern int free() ;
extern void numout() ;
extern void mhexout() ;
extern void cmdout() ;
extern long unpack() ;
extern void flip() ;
extern void specialout() ;
extern long getlong() ;
extern void error() ;
extern char *nextstring ;
/*
 *   These are the external variables we access.
 */
extern FILE *bitfile ;
extern fontdesctype *curfnt ;
extern long bytesleft ;
extern quarterword *raster ;
extern Boolean compressed ;
extern integer mag ;
/*
 *   We might use malloc here.
 */
extern char *malloc() ;
static unsigned char dummyend[8] = { 252 } ;
/*
 *   We have a routine that downloads an individual character.
 */
static int lastccout ;
void downchar(c, cc)
chardesctype *c ;
shalfword cc ;
{
   register long i, j ;
   register halfword cheight, cwidth ;
   register long k ;
   register quarterword *p ;
   register halfword cmd ;
   register shalfword xoff, yoff ;
   halfword wwidth = 0 ;
   register long len ;
   int smallchar ;

   p = c->packptr ;
   cmd = *p++ ;
   if (cmd & 4) {
      if ((cmd & 7) == 7) {
         cwidth = getlong(p) ;
         cheight = getlong(p + 4) ;
         xoff = getlong(p + 8) ;
         yoff = getlong(p + 12) ;
         p += 16 ;
      } else {
         cwidth = p[0] * 256 + p[1] ;
         cheight = p[2] * 256 + p[3] ;
         xoff = p[4] * 256 + p[5] ; /* N.B.: xoff, yoff are signed halfwords */
         yoff = p[6] * 256 + p[7] ;
         p += 8 ;
      }
   } else {
      cwidth = *p++ ;
      cheight = *p++ ;
      xoff = *p++ ;
      yoff = *p++ ;
      if (xoff > 127)
         xoff -= 256 ;
      if (yoff > 127)
         yoff -= 256 ;
   }
   if (c->flags & BIGCHAR)
      smallchar = 0 ;
   else
      smallchar = 5 ;
   if (compressed) {
      len = getlong(p) ;
      p += 4 ;
   } else {
      wwidth = (cwidth + 15) / 16 ;
      i = 2 * cheight * (long)wwidth ;
      if (i <= 0)
         i = 2 ;
      i += smallchar ;
      if (bytesleft < i) {
         if (bytesleft >= RASTERCHUNK)
            (void) free((char *) raster) ;
         if (RASTERCHUNK > i) {
            raster = (quarterword *)malloc(RASTERCHUNK) ;
            bytesleft = RASTERCHUNK ;
         } else {
            raster = (quarterword *)malloc((unsigned)i) ;
            bytesleft = i ;
         }
         if (raster == NULL) {
            error("! out of memory during allocation") ;
         }
      }
      k = i;
      while (k > 0)
         raster[--k] = 0 ;
      unpack(p, (halfword *)raster, cwidth, cheight, cmd) ;
      p = raster ;
      len = i - smallchar ;
   }
   if (cheight == 0 || cwidth == 0 || len == 0) {
      cwidth = 1 ;
      cheight = 1 ;
      wwidth = 1 ;
      len = 2 ;
      if (compressed)
         p = dummyend ;  /* CMD(END); see repack.c */
      else
         raster[0] = 0 ;
   }
   if (smallchar) {
      p[len] = cwidth ;
      p[len + 1] = cheight ;
      p[len + 2] = xoff + 128 ;
      p[len + 3] = yoff + 128 ;
      p[len + 4] = c->pixelwidth ;
   } else
/*
 *   Now we actually send out the data.
 */
      specialout('[') ;
   if (compressed) {
      specialout('<') ;
      mhexout(p, len + smallchar) ;
      specialout('>') ;
   } else {
      i = (cwidth + 7) / 8 ;
      if (i * cheight > 65520) {
         long bc = 0 ;
         specialout('<') ;
         for (j=0; j<cheight; j++) {
            if (bc + i > 65520) {
               specialout('>') ;
               specialout('<') ;
               bc = 0 ;
            }
            mhexout(p, i) ;
            bc += i ;
            p += 2*wwidth ;
         }
         specialout('>') ;
      } else {
         specialout('<') ;
         if (2 * wwidth == i)
            mhexout(p, ((long)cheight) * i + smallchar) ;
         else {
            for (j=0; j<cheight; j++) {
               mhexout(p, i) ;
               p += 2*wwidth ;
            }
            if (smallchar)
               mhexout(p, (long)smallchar) ;
         }
         specialout('>') ;
      }
   }
   if (smallchar == 0) {
      numout((integer)cwidth) ;
      numout((integer)cheight) ;
      numout((integer)xoff + 128) ; /* not all these casts needed. */
      numout((integer)yoff + 128) ;
      numout((integer)(c->pixelwidth)) ;
   }
   if (lastccout + 1 == cc) {
      cmdout("I") ;
   } else {
      numout((integer)cc) ;
      cmdout("D") ;
   }
   lastccout = cc ;
}
/*
 * Output the literal name of the font change command with PostScript index n
 */
void
lfontout(n)
int n ;
{
	char buf[10];
	if (n < 27)
		(void)sprintf(buf, "/f%c", 'a'+n-1) ;
	else
		(void)sprintf(buf, "/f%d", n-27) ;
	cmdout(buf);
}
static char goodnames[] =
   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" ;
void makepsname(s, n)
register char *s ;
register int n ;
{
   n-- ;
   *s++ = 'F' + n / sizeof(goodnames) ;
   *s++ = goodnames[n % sizeof(goodnames)] ;
   *s++ = 0 ;
}
/*
 *   And the download procedure.
 */
void download(p, psfont)
charusetype *p ;
int psfont ;
{
   register halfword b, bit ;
   register chardesctype *c ;
   int cc, maxcc = -1, numcc ;
   char name[10] ;

   lastccout = -5 ;
   name[0] = '/' ;
   makepsname(name + 1, psfont) ;
   curfnt = p->fd ;
   curfnt->psname = psfont ;
   if (curfnt->resfont) {
      struct resfont *rf = curfnt->resfont ;
/*
 *   I'm not sure why we tried to download each font only
 *   once here---we definitely need it each time . . .
 */
/*    if (rf->sent == 0) { */
         cmdout(name) ;
         cmdout("[") ;
         c = curfnt->chardesc ;
         for (cc=0; cc<256; cc++, c++)
            numout((integer)c->pixelwidth) ;
         cmdout("]") ;
         (void)strcpy(nextstring, "/") ;
         (void)strcat(nextstring, rf->PSname) ;
         cmdout(nextstring) ;
         if (rf->specialinstructions)
               cmdout(rf->specialinstructions) ;
         (void)sprintf(nextstring, "%ld", mag) ;
         cmdout(nextstring) ;
         (void)sprintf(nextstring, "%ld", curfnt->scaledsize) ;
         cmdout(nextstring) ;
         cmdout("rf") ;
         rf->sent = 1 ;
/*    } */
      return ;
   }
/*
 *   Here we calculate the largest character actually used, and
 *   send it as a parameter to df.
 */
   cc = 0 ;
   numcc = 0 ;
   for (b=0; b<16; b++) {
      for (bit=32768; bit!=0; bit>>=1) {
         if (p->bitmap[b] & bit) {
            maxcc = cc ;
            numcc++ ;
         }
         cc++ ;
      }
   }
   if (numcc <= 0)
      return ;
   cmdout(name) ;
   numout((integer)numcc) ;
   numout((integer)maxcc + 1) ;
/*
 *   If we need to scale the font, we say so by using dfs
 *   instead of df, and we give it a scale factor.  We also
 *   scale the character widths; this is ugly, but scaling
 *   fonts is ugly, and this is the best we can probably do.
 */
   if (curfnt->dpi != curfnt->loadeddpi) {
      numout((integer)curfnt->dpi) ;
      numout((integer)curfnt->loadeddpi) ;
      if (curfnt->alreadyscaled == 0) {
         for (b=0, c=curfnt->chardesc; b<256; b++, c++)
            c->pixelwidth = (c->pixelwidth * 
      (long)curfnt->dpi * 2 + curfnt->loadeddpi) / (2 * curfnt->loadeddpi) ;
         curfnt->alreadyscaled = 1 ;
      }
      cmdout("dfs") ;
   } else
      cmdout("df") ;
   c = curfnt->chardesc ;
   cc = 0 ;
   for (b=0; b<16; b++) {
      for (bit=32768; bit; bit>>=1) {
         if (p->bitmap[b] & bit) {
            downchar(c, cc) ;
            c->flags |= EXISTS ;
         } else
            c->flags &= ~EXISTS ;
         c++ ;
         cc++ ;
      }
   }
   cmdout("E") ;
}