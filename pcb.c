/*!
 * \file pcb.c
 *
 * \author Copyright (C) 2011 by Bert Timmerman <bert.timmerman@xs4all.nl>
 *
 * \brief Dump objects in the PCB footprint format.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.\n
 * \n
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.\n
 * \n
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.\n
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "coord.h"
#include "inst.h"
#include "pcb.h"
#include "file.h"


/*!
 * \brief .
 * 
 */
static unit_type
pcb_zeroize (unit_type n)
{
        return n == -1 || n == 1 ? 0 : n;
}


/*!
 * \brief .
 * 
 */
static void
pcb_centric
(
        struct coord a,
        struct coord b,
        struct coord *center,
        struct coord *size
)
{
        struct coord min;
        struct coord max;

        min.x = units_to_pcb (a.x);
        min.y = units_to_pcb (a.y);
        max.x = units_to_pcb (b.x);
        max.y = units_to_pcb (b.y);
        sort_coord (&min, &max);
        size->x = max.x - min.x;
        size->y = max.y - min.y;
        center->x = (min.x + max.x) / 2;
        center->y = -(min.y + max.y) / 2;
}


/*!
 * \brief .
 * 
 */
static void
pcb_do_drill
(
        FILE *file,
        const struct inst *pad,
        struct coord *ref
)
{
        const struct inst *hole = pad->u.pad.hole;
        struct coord center;
        struct coord size;

        if (!hole)
        {
                return;
        }
        pcb_centric (hole->base, hole->u.hole.other, &center, &size);
        /* Allow for rounding errors  */
        fprintf
        (
                file,
                "Pin[%d %d %d",
                size.x,
                -pcb_zeroize (center.x - ref->x),
                -pcb_zeroize (center.y - ref->y)
        );
        if (size.x < size.y - 1 || size.x > size.y + 1)
        {
                fprintf
                (
                        file,
                        " O %d %d",
                        size.x,
                        size.y);
        }
        fprintf (file, "]\n");
        *ref = center;
}


/*!
 * \brief Print information for a \c pad in the PCB footprint \c file . \n
 * 
 * Current PCB format: \n
 * \t Pad [rX1 rY1 rX2 rY2 Thickness Clearance Mask "Name" "Number" SFlags] \n
 * \n
 * Old PCB formats: \n
 * Pad (rX1 rY1 rX2 rY2 Thickness Clearance Mask "Name" "Number" NFlags) \n
 * Pad (aX1 aY1 aX2 aY2 Thickness "Name" "Number" NFlags) \n
 * Pad (aX1 aY1 aX2 aY2 Thickness "Name" NFlags) \n
 * \n
 * Legenda: \n
 * rX1 rY1 rX2 rY2 \n
 * \tCoordinates of the endpoints of the pad, relative to the element's mark. \n
 * \tNote that the copper extends beyond these coordinates by half the thickness. \n
 * \tTo make a square or round pad, specify the same coordinate twice. \n
 * aX1 aY1 aX2 aY2 \n
 * \tSame, but absolute coordinates of the endpoints of the pad. \n
 * Thickness \n
 * \tWidth of the pad. \n
 * Clearance \n
 * \tAdd to thickness to get clearance width. \n
 * Mask \n
 * \tWidth of solder mask opening. \n
 * Name \n
 * \tName of pad. \n
 * Number \n
 * \tNumber of pad. \n
 * SFlags \n
 * \tSymbolic or numerical flags. \n
 * NFlags \n
 * \tNumerical flags only. \n
 * 
 * \todo Implement \c pad_number, \c thickness, \c clearance and \c pad_solder_mask_clearance.
 */
static void
pcb_pad
(
        FILE *file,
        const struct inst *inst
)
{
        struct coord center;
        struct coord size;
        char *pad_number;
        char *pad_name;
        double rx1;
        double ry1;
        double rx2;
        double ry2;
        double pad_thickness;
        double pad_clearance;
        double pad_solder_mask_clearance;
        char *pad_flags;

        /* Convert fpd dimensions to PCB dimensions. */
        pcb_centric (inst->base, inst->u.pad.other, &center, &size);
        rx1 = (int) (center.x); /* rX1 coordinate */
        ry1 = (int) (center.y); /* rY1 coordinate */
        rx2 = (int) (center.x); /* rX2 coordinate */
        ry2 = (int) (center.y); /* rY2 coordinate */
        pad_thickness = (int) size.x; /*! \todo Thickness */
        pad_clearance = (int) size.x; /*! \todo Clearance */
        pad_solder_mask_clearance = (int) size.x; /* Mask */
        pad_name = strdup (inst->u.pad.name); /* Name */
        pad_number = strdup (inst->u.pad.name); /*! \todo Number */
        pad_flags = inst->obj->u.pad.rounded ? strdup ("") : strdup ("square"); /* SFlags */
        /* Write to PCB footprint file. */
        fprintf
        (
                file,
                "\tPad[%d %d %d %d %d %d %d \"%s\" \"%s\" \"%s\"]\n",
                (int) rx1,
                (int) ry1,
                (int) rx2,
                (int) ry2,
                (int) pad_thickness,
                (int) pad_clearance,
                (int) pad_solder_mask_clearance,
                pad_name,
                pad_number,
                pad_flags
        );
}


/*!
 * \brief Print information for a \c pin in the PCB footprint \c file . \n
 * 
 * Current PCB format: \n
 * Pin [rX rY Thickness Clearance Mask Drill "Name" "Number" SFlags] \n
 * \n
 * Old PCB formats: \n
 * Pin (rX rY Thickness Clearance Mask Drill "Name" "Number" NFlags) \n
 * Pin (aX aY Thickness Drill "Name" "Number" NFlags) \n
 * Pin (aX aY Thickness Drill "Name" NFlags) \n
 * Pin (aX aY Thickness "Name" NFlags)\n
 * \n
 * Legenda: \n
 * rX rY \n
 * \tCoordinates of center, relative to the element's mark. \n
 * aX aY \n
 * \tAbsolute coordinates of center. \n
 * Thickness \n
 * \tWidth of the pad. \n
 * Clearance \n
 * \tAdd to thickness to get clearance width. \n
 * Mask \n
 * \tWidth of solder mask opening. \n
 * Drill \n
 * \tDiameter of drill. \n
 * Name \n
 * \tName of pad. \n
 * Number \n
 * \tNumber of pad. \n
 * SFlags \n
 * \tSymbolic or numerical flags. \n
 * NFlags \n
 * \tNumerical flags only. \n
 * 
 * \todo Implement \c pin_number, \c thickness and \c Clearance.
 */
static void
pcb_hole
(
        FILE *file,
        const struct inst *inst
)
{
        struct coord center, size;
        char *pin_number;
        char *pin_name;
        double rx;
        double ry;
        double pin_pad_thickness;
        double pin_pad_clearance;
        double pin_pad_solder_mask_clearance;
        double pin_hole_drill;
        char *pin_flags;

        if (inst->u.hole.pad)
        {
                return;
        }
        /* Convert fpd dimensions to PCB dimensions. */
        pcb_centric (inst->base, inst->u.hole.other, &center, &size);
        rx = (int) (center.x); /* rX coordinate */
        ry = (int) (center.y); /* rY coordinate */
        pin_pad_thickness = (int) 0; /*! \todo Thickness */
        pin_pad_clearance = (int) 0; /*! \todo Clearance */
        pin_pad_solder_mask_clearance = (int) 0; /* Mask */
        pin_name = strdup (inst->u.pad.name); /* Name */
        pin_number = strdup (inst->u.pad.name); /*! \todo Number */
        pin_hole_drill = (int) (size.x); /* Drill */
        pin_flags = inst->obj->u.pad.rounded ? strdup ("hole") : strdup ("hole,square"); /* SFlags */
        /* Write to PCB footprint file. */
        fprintf
        (
                file,
                "\tPin[%d %d %d %d %d %d \"%s\" \"%s\" \"%s\"]\n",
                (int) rx,
                (int) ry,
                (int) pin_pad_thickness,
                (int) pin_pad_clearance,
                (int) pin_pad_solder_mask_clearance,
                (int) pin_hole_drill,
                pin_name,
                pin_number,
                pin_flags
        );
}


/*!
 * \brief Print information for a \c line (top silkscreen) in the PCB
 * footprint \c file . \n
 * 
 * Current PCB format: \n
 * ElementLine [X1 Y1 X2 Y2 Thickness] \n
 * \n
 * Old PCB formats: \n
 * ElementLine (X1 Y1 X2 Y2 Thickness) \n
 * \n
 * Legenda: \n
 * X1 Y1 X2 Y2 \n
 * \tCoordinates of the endpoints of the line. \n
 * \tThese are relative to the Element's mark point for new element formats, or absolute for older formats. \n
 * Thickness \n
 * \tThe width of the silk for this line. \n
 */
static void
pcb_line
(
        FILE *file,
        const struct inst *inst
)
{
        double rx1;
        double ry1;
        double rx2;
        double ry2;
        double line_thickness;
        /* Convert fpd dimensions to PCB dimensions. */
        rx1 = units_to_pcb (inst->base.x);
        ry1 = -units_to_pcb (inst->base.y);
        rx2 = units_to_pcb (inst->u.rect.end.x);
        ry2 = -units_to_pcb (inst->u.rect.end.y);
        line_thickness = units_to_pcb (inst->u.rect.width);
        /* Write to PCB footprint file. */
        fprintf
        (
                file,
                "\tElementLine[%d %d %d %d %d]\n",
                (int) rx1,
                (int) ry1,
                (int) rx2,
                (int) ry2,
                (int) line_thickness
        );
}


/*!
 * \brief Print information for a rectangle made of ElementLines in the
 * PCB footprint \c file . \n
 */
static void
pcb_rect
(
        FILE *file,
        const struct inst *inst
)
{
        double xmin;
        double ymin;
        double xmax;
        double ymax;
        double line_width;

        /* Convert fpd dimensions to PCB dimensions. */
        xmin = units_to_pcb (inst->base.x);
        ymin = units_to_pcb (inst->base.y);
        xmax= units_to_pcb (inst->u.rect.end.x);
        ymax= units_to_pcb (inst->u.rect.end.y);
        line_width = units_to_pcb (inst->u.rect.width);
        /* Print rectangle ends (perpendicular to x-axis) */
        fprintf
        (
                file,
                "\tElementLine[%d %d %d %d %d]\n",
                (int) xmin,
                (int) ymin,
                (int) xmin,
                (int) ymax,
                (int) line_width
        );
        fprintf
        (
                file,
                "\tElementLine[%d %d %d %d %d]\n",
                (int) xmax,
                (int) ymin,
                (int) xmax,
                (int) ymax,
                (int) line_width
        );
        /* Print rectangle sides (parallel with x-axis) */
        fprintf
        (
                file,
                "\tElementLine[%d %d %d %d %d]\n",
                (int) xmin,
                (int) ymin,
                (int) xmax,
                (int) ymin,
                (int) line_width
        );
        fprintf
        (
                file,
                "\tElementLine[%d %d %d %d %d]\n",
                (int) xmax,
                (int) ymax,
                (int) xmin,
                (int) ymax,
                (int) line_width
        );
}


/*!
 * \brief Print information for a 360 degree \c arc (top silkscreen) in
 * the PCB footprint \c file . \n
 * 
 * Current PCB format: \n
 * ElementArc [X Y Width Height StartAngle DeltaAngle Thickness] \n
 * \n
 * Old PCB formats: \n
 * ElementArc (X Y Width Height StartAngle DeltaAngle Thickness) \n
 * \n
 * Legenda: \n
 * X Y \n
 * \tCoordinates of the center of the arc. \n
 * \tThese are relative to the Element's mark point for new element formats, or absolute for older formats. \n
 * Width Height \n
 * \tThe width and height, from the center to the edge. \n
 * \tThe bounds of the circle of which this arc is a segment, is thus 2*Width by 2*Height. \n
 * StartAngle \n
 * \tThe angle of one end of the arc, in degrees. \n
 * \tIn PCB, an angle of zero points left (negative X direction), and 90 degrees points down (positive Y direction). \n
 * DeltaAngle \n
 * \tThe sweep of the arc. \n
 * \tThis may be negative. Positive angles sweep counterclockwise. \n
 * Thickness \n
 * \tThe width of the silk for which forms the arc. \n
 */
static void
pcb_circ
(
        FILE *file,
        const struct inst *inst
)
{
        double x;
        double y;
        double width;
        double height;
        double start_angle;
        double delta_angle;
        double line_width;

        /* Convert fpd dimensions to PCB dimensions. */
        x = units_to_pcb (inst->base.x);
        y = -units_to_pcb (inst->base.y);
        width = units_to_pcb (inst->u.arc.r);
        height = units_to_pcb (inst->u.arc.r);
        start_angle = 0;
        delta_angle = 360;
        line_width = units_to_pcb (inst->u.arc.width);
        /* Write to PCB footprint file. */
        fprintf
        (
                file,
                "\tElementArc[%d %d %d %d %d %d %d]\n",
                (int) x,
                (int) y,
                (int) width,
                (int) height,
                (int) start_angle,
                (int) delta_angle,
                (int) line_width
        );
}


/*!
 * \brief Print information for an \c arc (top silkscreen) in the PCB
 * footprint \c file . \n
 * 
 * Current PCB format: \n
 * ElementArc [X Y Width Height StartAngle DeltaAngle Thickness] \n
 * \n
 * Old PCB formats: \n
 * ElementArc (X Y Width Height StartAngle DeltaAngle Thickness) \n
 * \n
 * Legenda: \n
 * X Y \n
 * \tCoordinates of the center of the arc. \n
 * \tThese are relative to the Element's mark point for new element formats, or absolute for older formats. \n
 * Width Height \n
 * \tThe width and height, from the center to the edge. \n
 * \tThe bounds of the circle of which this arc is a segment, is thus 2*Width by 2*Height. \n
 * StartAngle \n
 * \tThe angle of one end of the arc, in degrees. \n
 * \tIn PCB, an angle of zero points left (negative X direction), and 90 degrees points down (positive Y direction). \n
 * DeltaAngle \n
 * \tThe sweep of the arc. \n
 * \tThis may be negative. Positive angles sweep counterclockwise. \n
 * Thickness \n
 * \tThe width of the silk for which forms the arc. \n
 */
static void
pcb_arc
(
        FILE *file,
        const struct inst *inst
)
{
        struct coord p;
        double a;
        double x;
        double y;
        double width;
        double height;
        double start_angle;
        double delta_angle;
        double line_width;

        p = rotate_r (inst->base, inst->u.arc.r, inst->u.arc.a2);
        a = inst->u.arc.a2 - inst->u.arc.a1;
        while (a <= 0)
        {
                a += 360;
        }
        while (a > 360)
        {
                a -= 360;
        }
        /* Convert fpd dimensions to PCB dimensions. */
        x = units_to_pcb (inst->base.x);
        y = -units_to_pcb (inst->base.y);
        width = units_to_pcb (inst->u.arc.r);
        height = units_to_pcb (inst->u.arc.r);
        start_angle = 0; /*! \todo Start angle */
        delta_angle = a * 10.0; /* Delta angle */
        line_width = units_to_pcb (inst->u.arc.width);
        /* Write to PCB footprint file. */
        fprintf
        (
                file,
                "\tElementArc[%d %d %d %d %d %d %d]\n",
                (int) x,
                (int) y,
                (int) width,
                (int) height,
                (int) start_angle,
                (int) delta_angle,
                (int) line_width
        );
}


/*!
 * \brief .
 * 
 */
static void
pcb_inst
(
        FILE *file,
        enum inst_prio prio,
        const struct inst *inst
)
{
        switch (prio)
        {
                case ip_pad_copper:
                        pcb_pad(file, inst);
                        break;
                case ip_pad_special:
                        pcb_pad(file, inst);
                        break;
                case ip_hole:
                        pcb_hole(file, inst);
                        break;
                case ip_line:
                        pcb_line(file, inst);
                        break;
                case ip_rect:
                        pcb_rect(file, inst);
                        break;
                case ip_circ:
                        pcb_circ(file, inst);
                        break;
                case ip_arc:
                        pcb_arc(file, inst);
                        break;
                default:
                        /* Don't try to export vectors, frame references, or measurements. */
                        break;
        }
}


/*!
 * \brief Print information for a \c header in the PCB footprint
 * \c file . \n
 * 
 * Elements may contain pins, pads, element lines, element arcs,
 * attributes, and (for older elements) an optional mark. \n
 * Note that element definitions that have the mark coordinates in the
 * element line, only support pins and pads which use relative
 * coordinates. \n
 * The pin and pad coordinates are relative to the mark. \n
 * Element definitions which do not include the mark coordinates in the
 * element line, may have a Mark definition in their contents, and only
 * use pin and pad definitions which use absolute coordinates. \n
 * \n
 * Current PCB format: \n
 * Element [SFlags "Desc" "Name" "Value" MX MY TX TY TDir TScale TSFlags] ( \n
 * \n
 * Old PCB formats: \n
 * Element (NFlags "Desc" "Name" "Value" MX MY TX TY TDir TScale TNFlags) ( \n
 * Element (NFlags "Desc" "Name" "Value" TX TY TDir TScale TNFlags) ( \n
 * Element (NFlags "Desc" "Name" TX TY TDir TScale TNFlags) ( \n
 * Element ("Desc" "Name" TX TY TDir TScale TNFlags) ( \n
 * ... contents ... \n
 * ) \n
 * \n
 * Legenda: \n
 * SFlags \n
 * \tSymbolic or numeric flags, for the element as a whole. \n
 * NFlags \n
 * \tNumeric flags, for the element as a whole. \n
 * Desc \n
 * \tThe description of the element. \n
 * \tThis is one of the three strings which can be displayed on the screen. \n
 * Name \n
 * \tThe name of the element, usually the reference designator. \n
 * Value \n
 * \tThe value of the element. \n
 * MX MY \n
 * \tThe location of the element's mark. \n
 * \tThis is the reference point for placing the element and its pins
 * and pads. \n
 * TX TY \n
 * \tThe upper left corner of the text (one of the three strings). \n
 * TDir \n
 * \tThe relative direction of the text. \n
 * \t0 means left to right for an unrotated element, 1 means up, 2 left,
 * 3 down. \n
 * TScale \n
 * \tSize of the text, as a percentage of the “default” size of of the
 * font (the default font is about 40 mils high). \n
 * Default is 100 (40 mils). \n
 * TSFlags \n
 * \tSymbolic or numeric flags, for the text. \n
 * TNFlags \n
 * \tNumeric flags, for the text. \n
 */
static void
pcb_footprint
(
        FILE *file,
        const struct pkg *pkg,
        time_t now
)
{
        enum inst_prio prio;
        const struct inst *inst;
        double x_text;
        double y_text;
        char *footprint_name;
        char *footprint_refdes;
        char *footprint_value;

        /* Convert fpd variables to PCB variables. */
        footprint_name = strdup (pkg->name);
        footprint_refdes = strdup ("");
        footprint_value = strdup ("");
        x_text = 0.0;
        y_text = 0.0;
        /* Write PCB footprint header to file */
        fprintf
        (
                file,
                "Element[\"\" \"%s\" \"%s?\" \"%s\" 0 0 %d %d 0 100 \"\"]\n(\n",
                footprint_name,
                footprint_refdes,
                footprint_value,
                (int) (x_text),
                (int) (y_text)
        );
        FOR_INST_PRIOS_UP (prio)
        {
                for (inst = pkgs->insts[prio]; inst; inst = inst->next)
                {
                        pcb_inst (file, prio, inst);
                }
                for (inst = pkg->insts[prio]; inst; inst = inst->next)
                {
                        pcb_inst (file, prio, inst);
                }
        }
        fprintf (file, ")\n\n");

}


/*!
 * \brief .
 * 
 */
int
pcb (FILE *file)
{
        const struct pkg *pkg;
        time_t now = time (NULL);

        fprintf (stdout, "fped: start of PCB footprint generation.\n");
        for (pkg = pkgs; pkg; pkg = pkg->next)
        {
                if (pkg->name)
                {
                        fprintf (stdout, "fped: generating a footprint for package: %s\n", pkg->name);
                        pcb_footprint (file, pkg, now);
                }
        }
        fprintf (stdout, "fped: end of PCB footprint generation.\n");
        fflush (file);
        fflush (stdout);
        return !ferror (file);
}

/* EOF */

