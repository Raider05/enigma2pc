/**
 * Copyright (C) 2001, 2002, 2003  Billy Biggs <vektor@dumbterm.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <limits.h>
#include <string.h>

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include "pulldown.h"

/**
 * scratch paper:
 *
 *  A A  A  B  B  C  C C  D D
 * [T B  T][B  T][B  T B][T B]
 * [1 1][2  2][3  3][4 4][5 5]
 * [C C]      [M  M][C C][C C]
 *  D A  A  A  B  B  C C  C D
 *
 * Top 1 : Drop
 * Bot 1 : Show
 * Top 2 : Drop
 * Bot 2 : Drop
 * Top 3 : Merge
 * Bot 3 : Drop
 * Top 4 : Show
 * Bot 4 : Drop
 * Top 5 : Drop
 * Bot 5 : Show
 *
 *   A A   1
 *   A B   2
 *   B C   4
 *   C C   8
 *   D D   16
 *
 *  D D A A A B B C C C D D A A A +------------
 * [       ]
 * [* *  ]                        | 1    top      AA
 *   [  * *]                      | 0 AA bottom   AA
 *
 *     [       ]
 *     [* *  ]                    | 1    top      AB
 *       [* *  ]                  | 1 AB bottom   AB
 *
 *         [       ]
 *         [  * *]                | 0    top      BC
 *           [* *  ]              | 1 BC bottom   BC
 *
 *             [       ]
 *             [  * *]            | 0    top      CC
 *               [  * *]          | 0 CC bottom   CC
 *
 *                 [       ]
 *                 [* *  ]        | 1    top      DD
 *                   [  * *]      | 0 DD bottom   DD
 *
 *
 *                     [* *  ]    | 1    top      AA
 *                       [  * *]  | 0 AA bottom   AA
 *
 */

/* Offset                               1     2     3      4      5   */
/* Field Pattern                       [T B  T][B  T][B   T B]  [T B] */
/* Action                              Copy  Save  Merge  Copy  Copy  */
/*                                           Bot   Top                */
static const int tff_top_pattern[] = { 0,    1,    0,     0,    0     };
static const int tff_bot_pattern[] = { 0,    0,    0,     1,    0     };

/* Offset                               1     2     3      4      5   */
/* Field Pattern                       [B T  B][T  B][T   B T]  [B T] */
/* Action                              Copy  Save  Merge  Copy  Copy  */
/*                                           Top   Bot                */
static const int bff_top_pattern[] = { 0,    0,    0,     1,    0     };
static const int bff_bot_pattern[] = { 0,    1,    0,     0,    0     };

/* Timestamp mangling                                            */
/* From the DVD :         0  +  3003+ 6006 + 9009+ 12012 = 15015 */
/* In 24fps time:         0  +      + 3754 + 7508+ 11262 = 15016 */

/**
 * Flag Pattern     Treat as
 * on DVD           last offset
 * ============================
 * T B T            bff 3
 * B T              bff 4
 * B T B            tff 3
 * T B              tff 4
 */

int determine_pulldown_offset( int top_repeat, int bot_repeat, int tff,
                               int last_offset )
{
    int predicted_offset;
    int pd_patterns = 0;
    int offset = -1;
    int exact = -1;
    int i;

    predicted_offset = last_offset << 1;
    if( predicted_offset > PULLDOWN_SEQ_DD ) predicted_offset = PULLDOWN_SEQ_AA;

    /**
     * Detect our pattern.
     */
    for( i = 0; i < 5; i++ ) {

        /**
         *  Truth table:
         *
         *  ref repeat,  frame repeat    valid
         *  ===========+==============+=======
         *   0           0            ->  1
         *   0           1            ->  1
         *   1           0            ->  0
         *   1           1            ->  1
         */

        if( tff ) {
            if(    ( !tff_top_pattern[ i ] || top_repeat )
                && ( !tff_bot_pattern[ i ] || bot_repeat ) ) {

                pd_patterns |= ( 1 << i );
                offset = i;
            }
        } else {
            if(    ( !bff_top_pattern[ i ] || top_repeat )
                && ( !bff_bot_pattern[ i ] || bot_repeat ) ) {

                pd_patterns |= ( 1 << i );
                offset = i;
            }
            if( bff_top_pattern[ i ] == top_repeat && bff_bot_pattern[ i ] == bot_repeat ) {
                exact = i;
            }
        }
    }

    offset = 1 << offset;

    /**
     * Check if the 3:2 pulldown pattern we previously decided on is
     * valid for this set.  If so, we use that.
     */
    if( pd_patterns & predicted_offset ) offset = predicted_offset;
    if( ( top_repeat || bot_repeat ) && exact > 0 ) offset = ( 1 << exact );

    return offset;
}

#define HISTORY_SIZE 5

static int tophistory[ 5 ];
static int bothistory[ 5 ];

static int tophistory_diff[ 5 ];
static int bothistory_diff[ 5 ];

static int histpos = 0;

#if 0  /* FIXME: unused */
static void fill_history( int tff )
{
    if( tff ) {
        tophistory[ 0 ] = INT_MAX; bothistory[ 0 ] = INT_MAX;
        tophistory[ 1 ] =       0; bothistory[ 1 ] = INT_MAX;
        tophistory[ 2 ] = INT_MAX; bothistory[ 2 ] = INT_MAX;
        tophistory[ 3 ] = INT_MAX; bothistory[ 3 ] =       0;
        tophistory[ 4 ] = INT_MAX; bothistory[ 3 ] = INT_MAX;

        tophistory_diff[ 0 ] = 0; bothistory_diff[ 0 ] = 0;
        tophistory_diff[ 1 ] = 1; bothistory_diff[ 1 ] = 0;
        tophistory_diff[ 2 ] = 0; bothistory_diff[ 2 ] = 0;
        tophistory_diff[ 3 ] = 0; bothistory_diff[ 3 ] = 1;
        tophistory_diff[ 4 ] = 0; bothistory_diff[ 3 ] = 0;
    } else {
        tophistory[ 0 ] = INT_MAX; bothistory[ 0 ] = INT_MAX;
        tophistory[ 1 ] = INT_MAX; bothistory[ 1 ] =       0;
        tophistory[ 2 ] = INT_MAX; bothistory[ 2 ] = INT_MAX;
        tophistory[ 3 ] =       0; bothistory[ 3 ] = INT_MAX;
        tophistory[ 4 ] = INT_MAX; bothistory[ 3 ] = INT_MAX;

        tophistory_diff[ 0 ] = 0; bothistory_diff[ 0 ] = 0;
        tophistory_diff[ 1 ] = 0; bothistory_diff[ 1 ] = 1;
        tophistory_diff[ 2 ] = 0; bothistory_diff[ 2 ] = 0;
        tophistory_diff[ 3 ] = 1; bothistory_diff[ 3 ] = 0;
        tophistory_diff[ 4 ] = 0; bothistory_diff[ 3 ] = 0;
    }

    histpos = 0;
}
#endif


int determine_pulldown_offset_history( int top_repeat, int bot_repeat, int tff, int *realbest )
{
    int avgbot = 0;
    int avgtop = 0;
    int best = 0;
    int min = -1;
    int minpos = 0;
    int minbot = 0;
    int j;
    int ret;
    int mintopval = -1;
    int mintoppos = -1;
    int minbotval = -1;
    int minbotpos = -1;

    tophistory[ histpos ] = top_repeat;
    bothistory[ histpos ] = bot_repeat;

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        avgtop += tophistory[ j ];
        avgbot += bothistory[ j ];
    }
    avgtop /= 5;
    avgbot /= 5;

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        // int cur = (tophistory[ j ] - avgtop);
        int cur = tophistory[ j ];
        if( cur < min || min < 0 ) {
            min = cur;
            minpos = j;
        }
        if( cur < mintopval || mintopval < 0 ) {
            mintopval = cur;
            mintoppos = j;
        }
    }

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        // int cur = (bothistory[ j ] - avgbot);
        int cur = bothistory[ j ];
        if( cur < min || min < 0 ) {
            min = cur;
            minpos = j;
            minbot = 1;
        }
        if( cur < minbotval || minbotval < 0 ) {
            minbotval = cur;
            minbotpos = j;
        }
    }

    if( minbot ) {
        best = tff ? ( minpos + 2 ) : ( minpos + 4 );
    } else {
        best = tff ? ( minpos + 4 ) : ( minpos + 2 );
    }
    best = best % HISTORY_SIZE;
    *realbest = 1 << ( ( histpos + (2*HISTORY_SIZE) - best ) % HISTORY_SIZE );

    best = (minbotpos + 2) % 5;
    ret  = 1 << ( ( histpos + (2*HISTORY_SIZE) - best ) % HISTORY_SIZE );
    best = (mintoppos + 4) % 5;
    ret |= 1 << ( ( histpos + (2*HISTORY_SIZE) - best ) % HISTORY_SIZE );

    histpos = (histpos + 1) % HISTORY_SIZE;
    return ret;
}

static int reference = 0;

int determine_pulldown_offset_history_new( int top_repeat, int bot_repeat, int tff, int predicted )
{
    int avgbot = 0;
    int avgtop = 0;
    int i, j;
    int ret;
    int mintopval = -1;
    int mintoppos = -1;
    int min2topval = -1;
    int min2toppos = -1;
    int minbotval = -1;
    int minbotpos = -1;
    int min2botval = -1;
    int min2botpos = -1;
    int predicted_pos = 0;

    tophistory[ histpos ] = top_repeat;
    bothistory[ histpos ] = bot_repeat;

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        avgtop += tophistory[ j ];
        avgbot += bothistory[ j ];
    }
    avgtop /= 5;
    avgbot /= 5;

    for( i = 0; i < 5; i++ ) { if( (1<<i) == predicted ) { predicted_pos = i; break; } }

    /*
    printf(top: %8d bot: %8d\ttop-avg: %8d bot-avg: %8d (%d)\n", top_repeat, bot_repeat, top_repeat - avgtop, bot_repeat - avgbot, (5 + predicted_pos - reference) % 5 );
    */

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        int cur = tophistory[ j ];
        if( cur < mintopval || mintopval < 0 ) {
            min2topval = mintopval;
            min2toppos = mintoppos;
            mintopval = cur;
            mintoppos = j;
        } else if( cur < min2topval || min2topval < 0 ) {
            min2topval = cur;
            min2toppos = j;
        }
    }

    for( j = 0; j < HISTORY_SIZE; j++ ) {
        int cur = bothistory[ j ];
        if( cur < minbotval || minbotval < 0 ) {
            min2botval = minbotval;
            min2botpos = minbotpos;
            minbotval = cur;
            minbotpos = j;
        } else if( cur < min2botval || min2botval < 0 ) {
            min2botval = cur;
            min2botpos = j;
        }
    }

    tophistory_diff[ histpos ] = ((mintoppos == histpos) || (min2toppos == histpos));
    bothistory_diff[ histpos ] = ((minbotpos == histpos) || (min2botpos == histpos));

    ret = 0;
    for( i = 0; i < 5; i++ ) {
        int valid = 1;
        for( j = 0; j < 5; j++ ) {
            // if( tff_top_pattern[ j ] && !tophistory_diff[ (i + j) % 5 ] && tophistory[ (i + j) % 5 ] != mintopval ) {
            if( tff_top_pattern[ j ] && (tophistory[ (i + j) % 5 ] > avgtop || !tophistory_diff[ (i + j) % 5 ]) ) {
                valid = 0;
                break;
            }
            // if( tff_bot_pattern[ j ] && !bothistory_diff[ (i + j) % 5 ] && bothistory[ (i + j) % 5 ] != minbotval ) {
            if( tff_bot_pattern[ j ] && (bothistory[ (i + j) % 5 ] > avgbot || !bothistory_diff[ (i + j) % 5 ]) ) {
                valid = 0;
                break;
            }
        }
        if( valid ) ret |= (1<<(((5-i)+histpos)%5));
    }

    /*
    printf( "ret: %d %d %d %d %d\n",
            PULLDOWN_OFFSET_1 & ret,
            PULLDOWN_OFFSET_2 & ret,
            PULLDOWN_OFFSET_3 & ret,
            PULLDOWN_OFFSET_4 & ret,
            PULLDOWN_OFFSET_5 & ret );
    */

    histpos = (histpos + 1) % HISTORY_SIZE;
    reference = (reference + 1) % 5;

    if( !ret ) {
        /* No pulldown sequence is valid, return an error. */
        return 0;
    } else if( !(predicted & ret) ) {
        /**
         * We have a valid sequence, but it doesn't match our prediction.
         * Return the first 'valid' sequence in the list.
         */
        for( i = 0; i < 5; i++ ) { if( ret & (1<<i) ) return (1<<i); }
    }

    /**
     * The predicted phase is still valid.
     */
    return predicted;
}

int determine_pulldown_offset_short_history_new( int top_repeat, int bot_repeat, int tff, int predicted )
{
    int avgbot = 0;
    int avgtop = 0;
    int i, j;
    int ret;
    int mintopval = -1;
    int mintoppos = -1;
    int min2topval = -1;
    int min2toppos = -1;
    int minbotval = -1;
    int minbotpos = -1;
    int min2botval = -1;
    int min2botpos = -1;
    int predicted_pos = 0;

    tophistory[ histpos ] = top_repeat;
    bothistory[ histpos ] = bot_repeat;

    for( j = 0; j < 3; j++ ) {
        avgtop += tophistory[ (histpos + 5 - j) % 5 ];
        avgbot += bothistory[ (histpos + 5 - j) % 5 ];
    }
    avgtop /= 3;
    avgbot /= 3;

    for( i = 0; i < 5; i++ ) { if( (1<<i) == predicted ) { predicted_pos = i; break; } }

    /*
    printf( "top: %8d bot: %8d\ttop-avg: %8d bot-avg: %8d (%d)\n",
            top_repeat, bot_repeat, top_repeat - avgtop, bot_repeat - avgbot,
            (5 + predicted_pos - reference) % 5 );
    */

    for( j = 0; j < 3; j++ ) {
        int cur = tophistory[ (histpos + 5 - j) % 5 ];
        if( cur < mintopval || mintopval < 0 ) {
            min2topval = mintopval;
            min2toppos = mintoppos;
            mintopval = cur;
            mintoppos = j;
        } else if( cur < min2topval || min2topval < 0 ) {
            min2topval = cur;
            min2toppos = j;
        }
    }

    for( j = 0; j < 3; j++ ) {
        int cur = bothistory[ (histpos + 5 - j) % 5 ];
        if( cur < minbotval || minbotval < 0 ) {
            min2botval = minbotval;
            min2botpos = minbotpos;
            minbotval = cur;
            minbotpos = j;
        } else if( cur < min2botval || min2botval < 0 ) {
            min2botval = cur;
            min2botpos = j;
        }
    }

    tophistory_diff[ histpos ] = ((mintoppos == histpos) || (min2toppos == histpos));
    bothistory_diff[ histpos ] = ((minbotpos == histpos) || (min2botpos == histpos));

    ret = 0;
    for( i = 0; i < 5; i++ ) {
        int valid = 1;
        for( j = 0; j < 3; j++ ) {
            // if( tff_top_pattern[ j ] && !tophistory_diff[ (i + j) % 5 ] && tophistory[ (i + j) % 5 ] != mintopval ) {
            // if( tff_top_pattern[ j ] && (tophistory[ (i + j) % 5 ] > avgtop || !tophistory_diff[ (i + j) % 5 ]) ) {
            if( tff_top_pattern[ (i + 5 - j) % 5 ] && tophistory[ (histpos + 5 - j) % 5 ] > avgtop ) {
            // if( tff_top_pattern[ (i + 5 - j) % 5 ] && !tophistory_diff[ (histpos + 5 - j) % 5 ] && tophistory[ (histpos + 5 - j) % 5 ] != mintopval ) {
                valid = 0;
                break;
            }
            // if( tff_bot_pattern[ j ] && !bothistory_diff[ (i + j) % 5 ] && bothistory[ (i + j) % 5 ] != minbotval ) {
            // if( tff_bot_pattern[ j ] && (bothistory[ (i + j) % 5 ] > avgbot || !bothistory_diff[ (i + j) % 5 ]) ) {
            if( tff_bot_pattern[ (i + 5 - j) % 5 ] && bothistory[ (histpos + 5 - j) % 5 ] > avgbot ) {
            // if( tff_bot_pattern[ (i + 5 - j) % 5 ] && !bothistory_diff[ (histpos + 5 - j) % 5 ] && bothistory[ (histpos + 5 - j) % 5 ] != minbotval ) {
                valid = 0;
                break;
            }
        }
        if( valid ) ret |= (1<<i);
    }

    /*
    printf( "ret: %d %d %d %d %d\n",
            PULLDOWN_OFFSET_1 & ret,
            PULLDOWN_OFFSET_2 & ret,
            PULLDOWN_OFFSET_3 & ret,
            PULLDOWN_OFFSET_4 & ret,
            PULLDOWN_OFFSET_5 & ret );
    */

    histpos = (histpos + 1) % HISTORY_SIZE;
    reference = (reference + 1) % 5;

    if( !ret ) {
        /* No pulldown sequence is valid, return an error. */
        return 0;
    } else if( !(predicted & ret) ) {
        /**
         * We have a valid sequence, but it doesn't match our prediction.
         * Return the first 'valid' sequence in the list.
         */
        for( i = 0; i < 5; i++ ) { if( ret & (1<<i) ) return (1<<i); }
    }

    /**
     * The predicted phase is still valid.
     */
    return predicted;
}

int determine_pulldown_offset_dalias( pulldown_metrics_t *old_peak,
                                      pulldown_metrics_t *old_relative,
                                      pulldown_metrics_t *old_mean,
                                      pulldown_metrics_t *new_peak,
                                      pulldown_metrics_t *new_relative,
                                      pulldown_metrics_t *new_mean )
{
    int laced = 0;

    if (old_peak->d > 360) {
        if (3*old_relative->e < old_relative->o) laced=1;
        if ((2*old_relative->d < old_relative->s) && (old_relative->s > 600))
            laced=1;
    }
    if (new_peak->d > 360) {
        if ((2*new_relative->t < new_relative->p) && (new_relative->p > 600))
            laced=1;
    }
    if( !laced ) return PULLDOWN_ACTION_NEXT_PREV;

    if (new_relative->t < 2*new_relative->p) {
        if ((3*old_relative->e < old_relative->o) || (2*new_relative->t < new_relative->p)) {
            return PULLDOWN_ACTION_PREV_NEXT;
        }
    }
    return PULLDOWN_ACTION_PREV_NEXT;
}

#define MAXUP(a,b) ((a) = ((a)>(b)) ? (a) : (b))

void diff_factor_packed422_frame( pulldown_metrics_t *peak, pulldown_metrics_t *rel, pulldown_metrics_t *mean,
                                  uint8_t *old, uint8_t *new, int w, int h, int os, int ns )
{
    int x, y;
    pulldown_metrics_t l;
    memset(peak, 0, sizeof(pulldown_metrics_t));
    memset(rel, 0, sizeof(pulldown_metrics_t));
    memset(mean, 0, sizeof(pulldown_metrics_t));
    for (y = 0; y < h-7; y += 8) {
        for (x = 8; x < w-8-7; x += 8) {
            diff_packed422_block8x8(&l, old+x+y*os, new+x+y*ns, os, ns);
            mean->d += l.d;
            mean->e += l.e;
            mean->o += l.o;
            mean->s += l.s;
            mean->p += l.p;
            mean->t += l.t;
            MAXUP(peak->d, l.d);
            MAXUP(peak->e, l.e);
            MAXUP(peak->o, l.o);
            MAXUP(peak->s, l.s);
            MAXUP(peak->p, l.p);
            MAXUP(peak->t, l.t);
            MAXUP(rel->e, l.e-l.o);
            MAXUP(rel->o, l.o-l.e);
            MAXUP(rel->s, l.s-l.t);
            MAXUP(rel->p, l.p-l.t);
            MAXUP(rel->t, l.t-l.p);
            MAXUP(rel->d, l.t-l.s); /* hack */
        }
    }
    x = (w/8-2)*(h/8);
    mean->d /= x;
    mean->e /= x;
    mean->o /= x;
    mean->s /= x;
    mean->p /= x;
    mean->t /= x;
}

int pulldown_source( int action, int bottom_field )
{
    if( action == PULLDOWN_SEQ_AA ) {
        return !bottom_field;
    } else if( action == PULLDOWN_SEQ_AB ) {
        return 1;
    } else if( action == PULLDOWN_SEQ_BC ) {
        return bottom_field;
    } else if( action == PULLDOWN_SEQ_CC ) {
        return 0;
    } else if( action == PULLDOWN_SEQ_DD ) {
        return !bottom_field;
    }

    return 0;
}

int pulldown_drop( int action, int bottom_field )
{
    int ret = 1;

    if( action == PULLDOWN_SEQ_AA && bottom_field )
        ret = 0;
    if( action == PULLDOWN_SEQ_BC && !bottom_field )
        ret = 0;
    if( action == PULLDOWN_SEQ_CC && !bottom_field )
        ret = 0;
    if( action == PULLDOWN_SEQ_DD && bottom_field )
        ret = 0;

    return ret;
}
