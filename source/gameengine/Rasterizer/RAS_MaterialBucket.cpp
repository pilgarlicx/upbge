/*
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
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/Rasterizer/RAS_MaterialBucket.cpp
 *  \ingroup bgerast
 */

#include "RAS_MaterialBucket.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_IRasterizer.h"
#include "RAS_MeshObject.h"
#include "RAS_Deformer.h"

#include <algorithm>

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#ifdef WIN32
#  include <windows.h>
#endif // WIN32

RAS_MaterialBucket::RAS_MaterialBucket(RAS_IPolyMaterial *mat)
{
	m_material = mat;
}

RAS_MaterialBucket::~RAS_MaterialBucket()
{
	for (RAS_MeshSlotList::iterator it = m_meshSlots.begin(), end = m_meshSlots.end(); it != end; ++it) {
		delete (*it);
	}
}

RAS_IPolyMaterial *RAS_MaterialBucket::GetPolyMaterial() const
{
	return m_material;
}

bool RAS_MaterialBucket::IsAlpha() const
{
	return (m_material->IsAlpha());
}

bool RAS_MaterialBucket::IsZSort() const
{
	return (m_material->IsZSort());
}

RAS_MeshSlot *RAS_MaterialBucket::AddMesh()
{
	RAS_MeshSlot *ms = new RAS_MeshSlot();
	ms->init(this);

	m_meshSlots.push_back(ms);

	return ms;
}

RAS_MeshSlot *RAS_MaterialBucket::CopyMesh(RAS_MeshSlot *ms)
{
	RAS_MeshSlot *newMeshSlot = new RAS_MeshSlot(*ms);
	m_meshSlots.push_back(newMeshSlot);

	return newMeshSlot;
}

void RAS_MaterialBucket::RemoveMesh(RAS_MeshSlot *ms)
{
	RAS_MeshSlotList::iterator it = std::find(m_meshSlots.begin(), m_meshSlots.end(), ms);
	if (it != m_meshSlots.end()) {
		m_meshSlots.erase(it);
		delete ms;
	}
}

RAS_MeshSlotList::iterator RAS_MaterialBucket::msBegin()
{
	return m_meshSlots.begin();
}

RAS_MeshSlotList::iterator RAS_MaterialBucket::msEnd()
{
	return m_meshSlots.end();
}

bool RAS_MaterialBucket::ActivateMaterial(const MT_Transform& cameratrans, RAS_IRasterizer *rasty)
{
	if (rasty->GetDrawingMode() == RAS_IRasterizer::KX_SHADOW && !m_material->CastsShadows())
		return false;

	if (rasty->GetDrawingMode() != RAS_IRasterizer::KX_SHADOW && m_material->OnlyShadow())
		return false;

	if (!rasty->SetMaterial(*m_material))
		return false;

	bool uselights = m_material->UsesLighting(rasty);
	rasty->ProcessLighting(uselights, cameratrans);

	return true;
}

void RAS_MaterialBucket::RenderMeshSlot(const MT_Transform& cameratrans, RAS_IRasterizer *rasty, RAS_MeshSlot *ms)
{
	m_material->ActivateMeshSlot(ms, rasty);

	if (ms->m_pDeformer) {
		ms->m_pDeformer->Apply(m_material);
	}

	if (IsZSort() && rasty->GetDrawingMode() >= RAS_IRasterizer::KX_SOLID)
		ms->m_mesh->SortPolygons(ms, cameratrans * MT_Transform(ms->m_OpenGLMatrix));

	rasty->PushMatrix();
	if (!ms->m_pDeformer || !ms->m_pDeformer->SkipVertexTransform()) {
		rasty->applyTransform(ms->m_OpenGLMatrix, m_material->GetDrawingMode());
	}

	if (rasty->QueryLists()) {
		if (ms->m_DisplayList)
			ms->m_DisplayList->SetModified(ms->m_mesh->GetModifiedFlag() & RAS_MeshObject::MESH_MODIFIED);
	}

	// verify if we can use display list, not for deformed object, and
	// also don't create a new display list when drawing shadow buffers,
	// then it won't have texture coordinates for actual drawing. also
	// for zsort we can't make a display list, since the polygon order
	// changes all the time.
	if (ms->m_pDeformer && ms->m_pDeformer->IsDynamic())
		ms->m_bDisplayList = false;
	else if (!ms->m_DisplayList && rasty->GetDrawingMode() == RAS_IRasterizer::KX_SHADOW)
		ms->m_bDisplayList = false;
	else if (IsZSort())
		ms->m_bDisplayList = false;
	else if (m_material->UsesObjectColor() && ms->m_bObjectColor)
		ms->m_bDisplayList = false;
	else if (ms->m_pDerivedMesh) {
		// Derived mesh are rendered by the viewport code.
		ms->m_bDisplayList = false;
	}
	else
		ms->m_bDisplayList = true;

	if (m_material->GetDrawingMode() & RAS_IRasterizer::RAS_RENDER_3DPOLYGON_TEXT) {
	    // for text drawing using faces
		rasty->IndexPrimitives_3DText(ms, m_material);
	}
	else {
		rasty->IndexPrimitives(ms);
	}

	rasty->PopMatrix();
}

void RAS_MaterialBucket::Optimize(MT_Scalar distance)
{
	/* TODO: still have to check before this works correct:
	 * - lightlayer, frontface, text, billboard
	 * - make it work with physics */

#if 0
	RAS_MeshSlotList::iterator it;
	RAS_MeshSlotList::iterator jt;

	// greed joining on all following buckets
	for (it = m_meshSlots.begin(); it != m_meshSlots.end(); it++)
		for (jt = it, jt++; jt != m_meshSlots.end(); jt++)
			jt->Join(&*it, distance);
#endif
}

RAS_DisplayArrayBucket *RAS_MaterialBucket::FindDisplayArrayBucket(RAS_DisplayArray *array)
{
	for (RAS_DisplayArrayBucketList::iterator it = m_displayArrayBucketList.begin(), end = m_displayArrayBucketList.end();
		it != end; ++it)
	{
		RAS_DisplayArrayBucket *displayArrayBucket = *it;
		if (displayArrayBucket->GetDisplayArray() == array) {
			return displayArrayBucket;
		}
	}
	RAS_DisplayArrayBucket *displayArrayBucket = new RAS_DisplayArrayBucket(this, array);
	return displayArrayBucket;
}

void RAS_MaterialBucket::AddDisplayArrayBucket(RAS_DisplayArrayBucket *bucket)
{
	m_displayArrayBucketList.push_back(bucket);
}

void RAS_MaterialBucket::RemoveDisplayArrayBucket(RAS_DisplayArrayBucket *bucket)
{
	RAS_DisplayArrayBucketList::iterator it = std::find(m_displayArrayBucketList.begin(), m_displayArrayBucketList.end(), bucket);
	if (it != m_displayArrayBucketList.end()) {
		m_displayArrayBucketList.erase(it);
	}
}

RAS_DisplayArrayBucketList& RAS_MaterialBucket::GetDisplayArrayBucketList()
{
	return m_displayArrayBucketList;
}
