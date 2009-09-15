/**
 * $Id$
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Contributor(s): Blender Foundation (2008), Juho Vepsäläinen
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include <stdlib.h>

#include "RNA_define.h"
#include "RNA_types.h"

#include "rna_internal.h"

#include "DNA_brush_types.h"
#include "DNA_texture_types.h"

EnumPropertyItem brush_sculpt_tool_items[] = {
	{SCULPT_TOOL_DRAW, "DRAW", 0, "Draw", ""},
	{SCULPT_TOOL_SMOOTH, "SMOOTH", 0, "Smooth", ""},
	{SCULPT_TOOL_PINCH, "PINCH", 0, "Pinch", ""},
	{SCULPT_TOOL_INFLATE, "INFLATE", 0, "Inflate", ""},
	{SCULPT_TOOL_GRAB, "GRAB", 0, "Grab", ""},
	{SCULPT_TOOL_LAYER, "LAYER", 0, "Layer", ""},
	{SCULPT_TOOL_FLATTEN, "FLATTEN", 0, "Flatten", ""},
	{SCULPT_TOOL_CLAY, "CLAY", 0, "Clay", ""},
	{0, NULL, 0, NULL, NULL}};

#ifdef RNA_RUNTIME

#include "MEM_guardedalloc.h"

#include "BKE_texture.h"

static void rna_Brush_mtex_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
	Brush *brush= (Brush*)ptr->data;
	rna_iterator_array_begin(iter, (void*)brush->mtex, sizeof(MTex*), MAX_MTEX, 0, NULL);
}

static PointerRNA rna_Brush_active_texture_get(PointerRNA *ptr)
{
	Brush *br= (Brush*)ptr->data;
	Tex *tex;

	tex= (br->mtex[(int)br->texact])? br->mtex[(int)br->texact]->tex: NULL;
	return rna_pointer_inherit_refine(ptr, &RNA_Texture, tex);
}

static void rna_Brush_active_texture_set(PointerRNA *ptr, PointerRNA value)
{
	Brush *br= (Brush*)ptr->data;
	int act= br->texact;

	if(br->mtex[act] && br->mtex[act]->tex)
		id_us_min(&br->mtex[act]->tex->id);

	if(value.data) {
		if(!br->mtex[act])
			br->mtex[act]= add_mtex();
		
		br->mtex[act]->tex= value.data;
		id_us_plus(&br->mtex[act]->tex->id);
	}
	else if(br->mtex[act]) {
		MEM_freeN(br->mtex[act]);
		br->mtex[act]= NULL;
	}
}

#else

void rna_def_brush(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;
	
	static EnumPropertyItem prop_blend_items[] = {
		{BRUSH_BLEND_MIX, "MIX", 0, "Mix", "Use mix blending mode while painting."},
		{BRUSH_BLEND_ADD, "ADD", 0, "Add", "Use add blending mode while painting."},
		{BRUSH_BLEND_SUB, "SUB", 0, "Subtract", "Use subtract blending mode while painting."},
		{BRUSH_BLEND_MUL, "MUL", 0, "Multiply", "Use multiply blending mode while painting."},
		{BRUSH_BLEND_LIGHTEN, "LIGHTEN", 0, "Lighten", "Use lighten blending mode while painting."},
		{BRUSH_BLEND_DARKEN, "DARKEN", 0, "Darken", "Use darken blending mode while painting."},
		{BRUSH_BLEND_ERASE_ALPHA, "ERASE_ALPHA", 0, "Erase Alpha", "Erase alpha while painting."},
		{BRUSH_BLEND_ADD_ALPHA, "ADD_ALPHA", 0, "Add Alpha", "Add alpha while painting."},
		{0, NULL, 0, NULL, NULL}};
		
	srna= RNA_def_struct(brna, "Brush", "ID");
	RNA_def_struct_ui_text(srna, "Brush", "Brush datablock for storing brush settings for painting and sculpting.");
	RNA_def_struct_ui_icon(srna, ICON_BRUSH_DATA);
	
	/* enums */
	prop= RNA_def_property(srna, "blend", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, prop_blend_items);
	RNA_def_property_ui_text(prop, "Blending mode", "Brush blending mode.");

	prop= RNA_def_property(srna, "sculpt_tool", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_items(prop, brush_sculpt_tool_items);
	RNA_def_property_ui_text(prop, "Sculpt Tool", "");
	
	/* number values */
	prop= RNA_def_property(srna, "size", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 1, 200);
	RNA_def_property_ui_text(prop, "Size", "Diameter of the brush.");
	
	prop= RNA_def_property(srna, "falloff", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "innerradius");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Falloff", "Falloff radius of the brush.");
	
	prop= RNA_def_property(srna, "spacing", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "spacing");
	RNA_def_property_range(prop, 1.0f, 100.0f);
	RNA_def_property_ui_text(prop, "Spacing", "Spacing between brush stamps.");

	prop= RNA_def_property(srna, "smooth_stroke_radius", PROP_INT, PROP_NONE);
	RNA_def_property_range(prop, 10, 200);
	RNA_def_property_ui_text(prop, "Smooth Stroke Radius", "Minimum distance from last point before stroke continues.");

	prop= RNA_def_property(srna, "smooth_stroke_factor", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.5, 0.99);
	RNA_def_property_ui_text(prop, "Smooth Stroke Factor", "Higher values give a smoother stroke.");
	
	prop= RNA_def_property(srna, "rate", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "rate");
	RNA_def_property_range(prop, 0.010f, 1.0f);
	RNA_def_property_ui_text(prop, "Rate", "Number of paints per second for Airbrush.");
	
	prop= RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR);
	RNA_def_property_float_sdna(prop, NULL, "rgb");
	RNA_def_property_ui_text(prop, "Color", "");
	
	prop= RNA_def_property(srna, "strength", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "alpha");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Strength", "The amount of pressure on the brush.");

	/* flag */
	prop= RNA_def_property(srna, "airbrush", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_AIRBRUSH);
	RNA_def_property_ui_text(prop, "Airbrush", "Keep applying paint effect while holding mouse (spray).");
	
	prop= RNA_def_property(srna, "wrap", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_TORUS);
	RNA_def_property_ui_text(prop, "Wrap", "Enable torus wrapping while painting.");
	
	prop= RNA_def_property(srna, "strength_pressure", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_ALPHA_PRESSURE);
	RNA_def_property_ui_text(prop, "Strength Pressure", "Enable tablet pressure sensitivity for strength.");
	
	prop= RNA_def_property(srna, "size_pressure", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_SIZE_PRESSURE);
	RNA_def_property_ui_text(prop, "Size Pressure", "Enable tablet pressure sensitivity for size.");
	
	prop= RNA_def_property(srna, "falloff_pressure", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_RAD_PRESSURE);
	RNA_def_property_ui_text(prop, "Falloff Pressure", "Enable tablet pressure sensitivity for falloff.");
	
	prop= RNA_def_property(srna, "spacing_pressure", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_SPACING_PRESSURE);
	RNA_def_property_ui_text(prop, "Spacing Pressure", "Enable tablet pressure sensitivity for spacing.");

	prop= RNA_def_property(srna, "rake", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_RAKE);
	RNA_def_property_ui_text(prop, "Rake", "Rotate the brush texture to match the stroke direction.");

	prop= RNA_def_property(srna, "anchored", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_ANCHORED);
	RNA_def_property_ui_text(prop, "Anchored", "Keep the brush anchored to the initial location.");

	prop= RNA_def_property(srna, "flip_direction", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_DIR_IN);
	RNA_def_property_ui_text(prop, "Flip Direction", "Move vertices in the opposite direction.");

	prop= RNA_def_property(srna, "space", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_SPACE);
	RNA_def_property_ui_text(prop, "Space", "Limit brush application to the distance specified by spacing.");

	prop= RNA_def_property(srna, "smooth_stroke", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_SMOOTH_STROKE);
	RNA_def_property_ui_text(prop, "Smooth Stroke", "Brush lags behind mouse and follows a smoother path.");

	prop= RNA_def_property(srna, "persistent", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_PERSISTENT);
	RNA_def_property_ui_text(prop, "Persistent", "Sculpts on a persistent layer of the mesh.");
	
	/* not exposed in the interface yet
	prop= RNA_def_property(srna, "fixed_tex", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", BRUSH_FIXED_TEX);
	RNA_def_property_ui_text(prop, "Fixed Texture", "Keep texture origin in fixed position.");*/

	prop= RNA_def_property(srna, "curve", PROP_POINTER, PROP_NEVER_NULL);
	RNA_def_property_ui_text(prop, "Curve", "Editable falloff curve.");

	/* texture */
	rna_def_mtex_common(srna, "rna_Brush_mtex_begin", "rna_Brush_active_texture_get",
		"rna_Brush_active_texture_set", "TextureSlot");

	/* clone tool */
	prop= RNA_def_property(srna, "clone_image", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "clone.image");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Clone Image", "Image for clone tool.");
	
	prop= RNA_def_property(srna, "clone_opacity", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "clone.alpha");
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Clone Opacity", "Opacity of clone image display.");

	prop= RNA_def_property(srna, "clone_offset", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_float_sdna(prop, NULL, "clone.offset");
	RNA_def_property_ui_text(prop, "Clone Offset", "");
	RNA_def_property_ui_range(prop, -1.0f , 1.0f, 10.0f, 3);
}


/* A brush stroke is a list of changes to the brush that
 * can occur during a stroke
 *
 *  o 3D location of the brush
 *  o 2D mouse location
 *  o Tablet pressure
 *  o Direction flip
 *  o Tool switch
 *  o Time
 */
static void rna_def_operator_stroke_element(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna= RNA_def_struct(brna, "OperatorStrokeElement", "IDPropertyGroup");
	RNA_def_struct_ui_text(srna, "Operator Stroke Element", "");

	prop= RNA_def_property(srna, "location", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Location", "");

	prop= RNA_def_property(srna, "mouse", PROP_FLOAT, PROP_XYZ);
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_array(prop, 2);
	RNA_def_property_ui_text(prop, "Mouse", "");

	prop= RNA_def_property(srna, "pressure", PROP_FLOAT, PROP_NONE);
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Pressure", "Tablet pressure.");

	prop= RNA_def_property(srna, "time", PROP_FLOAT, PROP_UNSIGNED);
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_ui_text(prop, "Time", "");

	prop= RNA_def_property(srna, "flip", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_flag(prop, PROP_IDPROPERTY);
	RNA_def_property_ui_text(prop, "Flip", "");

	/* XXX: Tool (this will be for pressing a modifier key for a different brush,
	        e.g. switching to a Smooth brush in the middle of the stroke */
}

void RNA_def_brush(BlenderRNA *brna)
{
	rna_def_brush(brna);
	rna_def_operator_stroke_element(brna);
}

#endif
