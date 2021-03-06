#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <math.h>
#include "ggraph.h"
#include "ggraphdefs.h"
/****************************************************************
 *								*
 *	hist - write the points of a histogramn			*
 *								*
 ****************************************************************/
hist ()
{
    register int     i;
    register float llabxl, llabyl;
    register float prevx, prevy;
    int clipped = 0;

    prevx = prevy = 0.0;	/* init values first */
    for (curline = 0; ((curline != cg.maxlines) && (cl != NULL)); ++curline) {
	if (cl->lonoff) {
	/* connect the dots */
	    if(cl->ctype){
	    fprintf (outfile, "%d\n", cl->ctype);
            prevx = ((0.0 - cg.xoffset) * cg.scalex) + cg.xorigin;
            prevy = ((0.0 - cg.yoffset) * cg.scaley) + cg.yorigin;
	    fprintf (outfile, "%4.1f %4.1f\n", prevx, prevy);
	    graphy = ((cl->ypoints[0] - cg.yoffset) * cg.scaley)
	      + cg.yorigin;
	    if((graphy < 0.0)||(graphy > 512.0) || 
		(prevx < 0.0)||(prevx > 512.0))
	      fprintf(stderr,"Point out of range\n");
	    else
		if((graphy < cg.yorigin)||(graphy > cg.yplotmax) || 
		      (prevx < cg.xorigin)||(prevx > cg.xplotmax)) {
			    fprintf(stderr,"%s:Point off graph clipped\n", 
				   graphname);
			    fprintf(outfile, 
				       (version == SUN_GREMLIN) ?
				       "*\n" : "-1.00 -1.00\n");
			    fprintf (outfile, "%d %d\n%d\n", cl->ltype, 0, 0);
			    fprintf (outfile, "%d\n", cl->ctype);
		    } 
		else
			fprintf (outfile, "%4.1f %4.1f\n", prevx, graphy);
	    prevx = ((cl->xpoints[0] - cg.xoffset) * cg.scalex)
	      + cg.xorigin;
	    for (i = 0; i < cl->maxpoint; i++) {
	        graphx = ((cl->xpoints[i] - cg.xoffset) * cg.scalex)
	          + cg.xorigin;
		graphy = ((cl->ypoints[i] - cg.yoffset) * cg.scaley)
		  + cg.yorigin;
	    if((graphx < 0.0)||(graphx > 512.0) || 
		(prevy < 0.0)||(prevy > 512.0))
		    fprintf(stderr,"Point out of range\n");
		else
		    if((graphy < cg.yorigin)||(graphy > cg.yplotmax) || 
			  (prevx < cg.xorigin)||(prevx > cg.xplotmax)) {
			    fprintf(stderr,"%s:Point off graph clipped\n", 
				   graphname);
			    if (!clipped) {
				    fprintf(outfile, 
				       (version == SUN_GREMLIN) ?
				       "*\n" : "-1.00 -1.00\n");
				    fprintf (outfile, "%d %d\n%d\n",
						cl->ltype, 0, 0);
				    clipped = 1;
			    } 
		    } 
		    else {
			    if (clipped) {
				    fprintf (outfile, "%d\n", cl->ctype);
				    clipped = 0;
			    } 
			    fprintf (outfile, "%4.1f %4.1f\n", prevx, graphy);
		    } 
	    if((graphy < 0.0)||(graphy > 512.0) || 
		(graphx < 0.0)||(graphx > 512.0))
		    fprintf(stderr,"Point out of range\n");
		else
		    if((graphy < cg.yorigin)||(graphy > cg.yplotmax) || 
			  (graphx < cg.xorigin)||(graphx > cg.xplotmax)) {
			    fprintf(stderr,"%s:Point off graph clipped\n", 
				   graphname);
			    if (!clipped) {
				    fprintf(outfile, 
				       (version == SUN_GREMLIN) ?
				       "*\n" : "-1.00 -1.00\n");
				    fprintf (outfile, "%d %d\n%d\n", 
						cl->ltype, 0, 0);
				    clipped = 1;
			    } 
		    } 
		    else {
			    if (clipped) {
				    fprintf (outfile, "%d\n", cl->ctype);
				    clipped = 0;
			    } 
			    fprintf (outfile, "%4.1f %4.1f\n", graphx, graphy);
		    } 
		prevx = graphx;
		prevy = graphy;
	    }
            prevy = ((0.0 - cg.yoffset) * cg.scaley) + cg.yorigin;
	    fprintf (outfile, "%4.1f %4.1f\n", prevx, prevy);
	    fprintf(outfile, (version == SUN_GREMLIN) ? "*\n" : "-1.00 -1.00\n");
	    fprintf (outfile, "%d %d\n%d\n", cl->ltype, 0, 0);
	    }
	    if (cl->llabsw) {
		if(cl->llabel.t_text[0] == NULL)
		    strcpy (cl->llabel, cl->lname);
		if(!cl->llabel.t_xpos)
		    llabxl = (cl->xpoints[cl->maxpoint-1] * cg.scalex)
			 + cg.xorigin - 10.0;
		else
		    llabxl = (cl->llabel.t_xpos * cg.scalex) + cg.xorigin - 10.0;
		if(!cl->llabel.t_ypos)
		    llabyl = (cl->ypoints[cl->maxpoint-1] * cg.scaley)
			 + cg.yorigin + 5.0;
		else
		    llabyl = (cl->llabel.t_ypos * cg.scaley)
		    + cg.yorigin + 5.0;
		drawctext (llabxl, llabyl, cl->llabel.t_font, cl->llabel.t_size,
		  cl->llabel.t_text, TOPCENTER_TEXT);
	    }
	}
    }
}

