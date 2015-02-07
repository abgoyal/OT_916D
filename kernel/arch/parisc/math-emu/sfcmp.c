


#include "float.h"
#include "sgl_float.h"
    
int
sgl_fcmp (sgl_floating_point * leftptr, sgl_floating_point * rightptr,
	  unsigned int cond, unsigned int *status)
                                           
                       /* The predicate to be tested */
                         
    {
    register unsigned int left, right;
    register int xorresult;
        
    /* Create local copies of the numbers */
    left = *leftptr;
    right = *rightptr;

    /*
     * Test for NaN
     */
    if(    (Sgl_exponent(left) == SGL_INFINITY_EXPONENT)
        || (Sgl_exponent(right) == SGL_INFINITY_EXPONENT) )
	{
	/* Check if a NaN is involved.  Signal an invalid exception when 
	 * comparing a signaling NaN or when comparing quiet NaNs and the
	 * low bit of the condition is set */
        if( (  (Sgl_exponent(left) == SGL_INFINITY_EXPONENT)
	    && Sgl_isnotzero_mantissa(left) 
	    && (Exception(cond) || Sgl_isone_signaling(left)))
	   ||
	    (  (Sgl_exponent(right) == SGL_INFINITY_EXPONENT)
	    && Sgl_isnotzero_mantissa(right) 
	    && (Exception(cond) || Sgl_isone_signaling(right)) ) )
	    {
	    if( Is_invalidtrap_enabled() ) {
	    	Set_status_cbit(Unordered(cond));
		return(INVALIDEXCEPTION);
	    }
	    else Set_invalidflag();
	    Set_status_cbit(Unordered(cond));
	    return(NOEXCEPTION);
	    }
	/* All the exceptional conditions are handled, now special case
	   NaN compares */
        else if( ((Sgl_exponent(left) == SGL_INFINITY_EXPONENT)
	    && Sgl_isnotzero_mantissa(left))
	   ||
	    ((Sgl_exponent(right) == SGL_INFINITY_EXPONENT)
	    && Sgl_isnotzero_mantissa(right)) )
	    {
	    /* NaNs always compare unordered. */
	    Set_status_cbit(Unordered(cond));
	    return(NOEXCEPTION);
	    }
	/* infinities will drop down to the normal compare mechanisms */
	}
    /* First compare for unequal signs => less or greater or
     * special equal case */
    Sgl_xortointp1(left,right,xorresult);
    if( xorresult < 0 )
        {
        /* left negative => less, left positive => greater.
         * equal is possible if both operands are zeros. */
        if( Sgl_iszero_exponentmantissa(left) 
	  && Sgl_iszero_exponentmantissa(right) )
            {
	    Set_status_cbit(Equal(cond));
	    }
	else if( Sgl_isone_sign(left) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
	else
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
        }
    /* Signs are the same.  Treat negative numbers separately
     * from the positives because of the reversed sense.  */
    else if( Sgl_all(left) == Sgl_all(right) )
        {
        Set_status_cbit(Equal(cond));
        }
    else if( Sgl_iszero_sign(left) )
        {
        /* Positive compare */
        if( Sgl_all(left) < Sgl_all(right) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
	else
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
	}
    else
        {
        /* Negative compare.  Signed or unsigned compares
         * both work the same.  That distinction is only
         * important when the sign bits differ. */
        if( Sgl_all(left) > Sgl_all(right) )
	    {
	    Set_status_cbit(Lessthan(cond));
	    }
        else
	    {
	    Set_status_cbit(Greaterthan(cond));
	    }
        }
	return(NOEXCEPTION);
    }
