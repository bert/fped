/*
 * layer.h - PCB layers on a pad
 *
 * Written 2009 by Werner Almesberger
 * Copyright 2009 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef LAYER_H
#define LAYER_H

#include <stdint.h>


typedef uint32_t layer_type;


enum kicad_layer {
	layer_bottom,		/* "copper" */
	layer_l15,
	layer_l14,
	layer_l13,
	layer_l12,
	layer_l11,
	layer_l10,
	layer_l9,
	layer_l8,
	layer_l7,
	layer_l6,
	layer_l5,
	layer_l4,
	layer_l3,
	layer_l2,
	layer_top,		/* "component" */
	layer_glue_bottom,	/* adhesive, copper side */
	layer_glue_top,		/* adhesive, component side */
	layer_paste_bottom,	/* solder paste */
	layer_paste_top,
	layer_silk_bottom,	/* silk screen */
	layer_silk_top,
	layer_mask_bottom,	/* solder mask */
	layer_mask_top,
	layer_draw,		/* general drawing */
	layer_comment,
	layer_eco1,
	layer_eco2,
	layer_edge,		/* edge */
};


enum pad_type {
	pt_normal,	/* copper and solder mask */
	pt_bare,	/* only copper (and finish) */
	pt_paste,	/* only solder paste */
	pt_mask,	/* only solder mask */
	pt_n
};


/*
 * Shorthands for the layers we use in a general sense.
 */

#define	LAYER_COPPER	(1 << layer_top)
#define	LAYER_PASTE	(1 << layer_paste_top)
#define	LAYER_MASK	(1 << layer_mask_top)


/*
 * pad_type_to_layers returns the initial set of layers. This set can then be
 * modified by overlaying other pads. For display purposes, we translate back
 * to the effective pad type with layers_to_pad_type.
 *
 * What this basically means is that pt_normal becomes pt_bare if its solder
 * paste mask has been removed.
 */

layer_type pad_type_to_layers(enum pad_type type);
enum pad_type layers_to_pad_type(layer_type layers);

#endif /* !LAYER_H */
