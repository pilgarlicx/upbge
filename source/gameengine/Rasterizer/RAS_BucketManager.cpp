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

/** \file gameengine/Rasterizer/RAS_BucketManager.cpp
 *  \ingroup bgerast
 */

#ifdef _MSC_VER
   /* don't show these anoying STL warnings */
#  pragma warning (disable:4786)
#endif

#include "RAS_MaterialBucket.h"
#include "RAS_MeshObject.h"
#include "RAS_Polygon.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_IRasterizer.h"

#include "RAS_BucketManager.h"

#include <algorithm>
/* sorting */

struct RAS_BucketManager::sortedmeshslot
{
public:
	MT_Scalar m_z;					/* depth */
	RAS_MeshSlot *m_ms;				/* mesh slot */
	RAS_MaterialBucket *m_bucket;	/* buck mesh slot came from */

	sortedmeshslot() {}

	void set(RAS_MeshSlot *ms, RAS_MaterialBucket *bucket, const MT_Vector3& pnorm)
	{
		// would be good to use the actual bounding box center instead
		MT_Point3 pos(ms->m_OpenGLMatrix[12], ms->m_OpenGLMatrix[13], ms->m_OpenGLMatrix[14]);

		m_z = MT_dot(pnorm, pos);
		m_ms = ms;
		m_bucket = bucket;
	}
};

struct RAS_BucketManager::backtofront
{
	bool operator()(const sortedmeshslot &a, const sortedmeshslot &b)
	{
		return (a.m_z < b.m_z) || (a.m_z == b.m_z && a.m_ms < b.m_ms);
	}
};

struct RAS_BucketManager::fronttoback
{
	bool operator()(const sortedmeshslot &a, const sortedmeshslot &b)
	{
		return (a.m_z > b.m_z) || (a.m_z == b.m_z && a.m_ms > b.m_ms);
	}
};

/* bucket manager */

RAS_BucketManager::RAS_BucketManager()
{

}

RAS_BucketManager::~RAS_BucketManager()
{
	BucketList::iterator it;

	for (it = m_SolidBuckets.begin(); it != m_SolidBuckets.end(); it++)
		delete (*it);

	for (it = m_AlphaBuckets.begin(); it != m_AlphaBuckets.end(); it++)
		delete(*it);
	
	m_SolidBuckets.clear();
	m_AlphaBuckets.clear();
}

void RAS_BucketManager::OrderBuckets(const MT_Transform& cameratrans, BucketList& buckets, std::vector<sortedmeshslot>& slots, bool alpha)
{
	BucketList::iterator bit;
	RAS_MeshSlotList::iterator mit;
	size_t size = 0, i = 0;

	/* Camera's near plane equation: pnorm.dot(point) + pval,
	 * but we leave out pval since it's constant anyway */
	const MT_Vector3 pnorm(cameratrans.getBasis()[2]);

	for (bit = buckets.begin(); bit != buckets.end(); ++bit) {
		RAS_DisplayArrayBucketList& displayArrayBucketList = (*bit)->GetDisplayArrayBucketList();
		for (RAS_DisplayArrayBucketList::iterator dbit = displayArrayBucketList.begin(), dbend = displayArrayBucketList.end();
			 dbit != dbend; ++dbit)
		{
			size += (*dbit)->GetNumActiveMeshSlots();
		}
	}

	slots.resize(size);

	for (bit = buckets.begin(); bit != buckets.end(); ++bit)
	{
		RAS_MaterialBucket* bucket = *bit;
		RAS_DisplayArrayBucketList& displayArrayBucketList = (*bit)->GetDisplayArrayBucketList();
		for (RAS_DisplayArrayBucketList::iterator dbit = displayArrayBucketList.begin(), dbend = displayArrayBucketList.end();
			 dbit != dbend; ++dbit)
		{
			RAS_DisplayArrayBucket *displayArrayBucket = *dbit;
			RAS_MeshSlotList& activeMeshSlots = displayArrayBucket->GetActiveMeshSlots();
			for (RAS_MeshSlotList::iterator it = activeMeshSlots.begin(), end = activeMeshSlots.end(); it != end; ++it) {
				slots[i++].set(*it, bucket, pnorm);
			}
			displayArrayBucket->RemoveActiveMeshSlots();
		}
	}
		
	if (alpha)
		sort(slots.begin(), slots.end(), backtofront());
	else
		sort(slots.begin(), slots.end(), fronttoback());
}

void RAS_BucketManager::RenderAlphaBuckets(const MT_Transform& cameratrans, RAS_IRasterizer* rasty)
{
	std::vector<sortedmeshslot> slots;
	std::vector<sortedmeshslot>::iterator sit;

	// Having depth masks disabled/enabled gives different artifacts in
	// case no sorting is done or is done inexact. For compatibility, we
	// disable it.
	if (rasty->GetDrawingMode() != RAS_IRasterizer::KX_SHADOW)
		rasty->SetDepthMask(RAS_IRasterizer::KX_DEPTHMASK_DISABLED);

	OrderBuckets(cameratrans, m_AlphaBuckets, slots, true);
	
	for (sit=slots.begin(); sit!=slots.end(); ++sit) {
		rasty->SetClientObject(sit->m_ms->m_clientObj);

		RAS_DisplayArray *displayArray = sit->m_ms->GetDisplayArray();
		rasty->BindPrimitives(displayArray);

		while (sit->m_bucket->ActivateMaterial(cameratrans, rasty))
			sit->m_bucket->RenderMeshSlot(cameratrans, rasty, sit->m_ms);

		rasty->UnbindPrimitives(displayArray);

		// make this mesh slot culled automatically for next frame
		// it will be culled out by frustrum culling
		sit->m_ms->SetCulled(true);
	}

	rasty->SetDepthMask(RAS_IRasterizer::KX_DEPTHMASK_ENABLED);
}

void RAS_BucketManager::RenderSolidBuckets(const MT_Transform& cameratrans, RAS_IRasterizer* rasty)
{
	BucketList::iterator bit;

	rasty->SetDepthMask(RAS_IRasterizer::KX_DEPTHMASK_ENABLED);

	for (bit = m_SolidBuckets.begin(); bit != m_SolidBuckets.end(); ++bit) {
#if 1
		RAS_MaterialBucket* bucket = *bit;
		RAS_DisplayArrayBucketList& displayArrayBucketList = bucket->GetDisplayArrayBucketList();
		for (RAS_DisplayArrayBucketList::iterator sbit = displayArrayBucketList.begin(), sbend = displayArrayBucketList.end();
			 sbit != sbend; ++sbit)
		{
			RAS_DisplayArrayBucket *displayArrayBucket = *sbit;
			RAS_MeshSlotList& activeMeshSlots = displayArrayBucket->GetActiveMeshSlots();
			if (displayArrayBucket->GetNumActiveMeshSlots() == 0) {
				continue;
			}

			RAS_DisplayArray *displayArray = displayArrayBucket->GetDisplayArray();

			rasty->BindPrimitives(displayArray);
			for (RAS_MeshSlotList::iterator mit = activeMeshSlots.begin(), mend = activeMeshSlots.end(); mit != mend; ++mit) {
				RAS_MeshSlot *ms = *mit;
				rasty->SetClientObject(ms->m_clientObj);
				while (bucket->ActivateMaterial(cameratrans, rasty))
					bucket->RenderMeshSlot(cameratrans, rasty, ms);

				// make this mesh slot culled automatically for next frame
				// it will be culled out by frustrum culling
				ms->SetCulled(true);
			}
			// Ensure we unset array attributs.
			rasty->UnbindPrimitives(displayArray);

			displayArrayBucket->RemoveActiveMeshSlots();
		}
#else
		RAS_MeshSlotList::iterator mit;
		for (mit = (*bit)->msBegin(); mit != (*bit)->msEnd(); ++mit) {
			if (mit->IsCulled())
				continue;

			rasty->SetClientObject(rasty, mit->m_clientObj);

			while ((*bit)->ActivateMaterial(cameratrans, rasty))
				(*bit)->RenderMeshSlot(cameratrans, rasty, *mit);

			// make this mesh slot culled automatically for next frame
			// it will be culled out by frustrum culling
			mit->SetCulled(true);
		}
#endif
	}
	
	/* this code draws meshes order front-to-back instead to reduce overdraw.
	 * it turned out slower due to much material state switching, a more clever
	 * algorithm might do better. */
#if 0
	vector<sortedmeshslot> slots;
	vector<sortedmeshslot>::iterator sit;

	OrderBuckets(cameratrans, m_SolidBuckets, slots, false);

	for (sit=slots.begin(); sit!=slots.end(); ++sit) {
		rendertools->SetClientObject(rasty, sit->m_ms->m_clientObj);

		while (sit->m_bucket->ActivateMaterial(cameratrans, rasty))
			sit->m_bucket->RenderMeshSlot(cameratrans, rasty, *(sit->m_ms));
	}
#endif
}

void RAS_BucketManager::Renderbuckets(const MT_Transform& cameratrans, RAS_IRasterizer* rasty)
{
	/* beginning each frame, clear (texture/material) caching information */
	rasty->ClearCachingInfo();

	RenderSolidBuckets(cameratrans, rasty);
	RenderAlphaBuckets(cameratrans, rasty);

	/* If we're drawing shadows and bucket wasn't rendered (outside of the lamp frustum or doesn't cast shadows)
	 * then the mesh is still modified, so we don't want to set MeshModified to false yet (it will mess up
	 * updating display lists). Just leave this step for the main render pass.
	 */
	if (rasty->GetDrawingMode() != RAS_IRasterizer::KX_SHADOW) {
		/* All meshes should be up to date now */
		/* Don't do this while processing buckets because some meshes are split between buckets */
		BucketList::iterator bit;
		RAS_MeshSlotList::iterator mit;
		for (bit = m_SolidBuckets.begin(); bit != m_SolidBuckets.end(); ++bit) {
			for (mit = (*bit)->msBegin(); mit != (*bit)->msEnd(); ++mit) {
				RAS_MeshObject *meshobj = (*mit)->m_mesh;
				meshobj->SetModifiedFlag(0);
			}
		}
		for (bit = m_AlphaBuckets.begin(); bit != m_AlphaBuckets.end(); ++bit) {
			for (mit = (*bit)->msBegin(); mit != (*bit)->msEnd(); ++mit) {
				RAS_MeshObject *meshobj = (*mit)->m_mesh;
				meshobj->SetModifiedFlag(0);
			}
		}
	}
	

	rasty->SetClientObject(NULL);
}

RAS_MaterialBucket *RAS_BucketManager::FindBucket(RAS_IPolyMaterial *material, bool &bucketCreated)
{
	BucketList::iterator it;

	bucketCreated = false;

	for (it = m_SolidBuckets.begin(); it != m_SolidBuckets.end(); it++)
		if (*(*it)->GetPolyMaterial() == *material)
			return *it;
	
	for (it = m_AlphaBuckets.begin(); it != m_AlphaBuckets.end(); it++)
		if (*(*it)->GetPolyMaterial() == *material)
			return *it;
	
	RAS_MaterialBucket *bucket = new RAS_MaterialBucket(material);
	bucketCreated = true;

	if (bucket->IsAlpha())
		m_AlphaBuckets.push_back(bucket);
	else
		m_SolidBuckets.push_back(bucket);
	
	return bucket;
}

void RAS_BucketManager::OptimizeBuckets(MT_Scalar distance)
{
	BucketList::iterator bit;
	
	distance = 10.0f;

	for (bit = m_SolidBuckets.begin(); bit != m_SolidBuckets.end(); ++bit)
		(*bit)->Optimize(distance);
	for (bit = m_AlphaBuckets.begin(); bit != m_AlphaBuckets.end(); ++bit)
		(*bit)->Optimize(distance);
}

void RAS_BucketManager::ReleaseDisplayLists(RAS_IPolyMaterial *mat)
{
	BucketList::iterator bit;
	RAS_MeshSlotList::iterator mit;

	for (bit = m_SolidBuckets.begin(); bit != m_SolidBuckets.end(); ++bit) {
		if (mat == NULL || (mat == (*bit)->GetPolyMaterial())) {
			for (mit = (*bit)->msBegin(); mit != (*bit)->msEnd(); ++mit) {
				RAS_MeshSlot *ms = *mit;
				if (ms->m_DisplayList) {
					ms->m_DisplayList->Release();
					ms->m_DisplayList = NULL;
				}
			}
		}
	}
	
	for (bit = m_AlphaBuckets.begin(); bit != m_AlphaBuckets.end(); ++bit) {
		if (mat == NULL || (mat == (*bit)->GetPolyMaterial())) {
			for (mit = (*bit)->msBegin(); mit != (*bit)->msEnd(); ++mit) {
				RAS_MeshSlot *ms = *mit;
				if (ms->m_DisplayList) {
					ms->m_DisplayList->Release();
					ms->m_DisplayList = NULL;
				}
			}
		}
	}
}

void RAS_BucketManager::ReleaseMaterials(RAS_IPolyMaterial * mat)
{
	BucketList::iterator bit;
	RAS_MeshSlotList::iterator mit;

	for (bit = m_SolidBuckets.begin(); bit != m_SolidBuckets.end(); ++bit) {
		if (mat == NULL || (mat == (*bit)->GetPolyMaterial())) {
			(*bit)->GetPolyMaterial()->ReleaseMaterial();
		}
	}
	
	for (bit = m_AlphaBuckets.begin(); bit != m_AlphaBuckets.end(); ++bit) {
		if (mat == NULL || (mat == (*bit)->GetPolyMaterial())) {
			(*bit)->GetPolyMaterial()->ReleaseMaterial();
		}
	}
}

/* frees the bucket, only used when freeing scenes */
void RAS_BucketManager::RemoveMaterial(RAS_IPolyMaterial * mat)
{
	BucketList::iterator bit, bitp;
	RAS_MeshSlotList::iterator mit;
	int i;


	for (i=0; i<m_SolidBuckets.size(); i++) {
		RAS_MaterialBucket *bucket = m_SolidBuckets[i];
		if (mat == bucket->GetPolyMaterial()) {
			m_SolidBuckets.erase(m_SolidBuckets.begin()+i);
			delete bucket;
			i--;
		}
	}

	for (int i=0; i<m_AlphaBuckets.size(); i++) {
		RAS_MaterialBucket *bucket = m_AlphaBuckets[i];
		if (mat == bucket->GetPolyMaterial()) {
			m_AlphaBuckets.erase(m_AlphaBuckets.begin()+i);
			delete bucket;
			i--;
		}
	}
}

//#include <stdio.h>

void RAS_BucketManager::MergeBucketManager(RAS_BucketManager *other, SCA_IScene *scene)
{
	/* concatinate lists */
	// printf("BEFORE %d %d\n", GetSolidBuckets().size(), GetAlphaBuckets().size());

	GetSolidBuckets().insert( GetSolidBuckets().end(), other->GetSolidBuckets().begin(), other->GetSolidBuckets().end() );
	other->GetSolidBuckets().clear();

	GetAlphaBuckets().insert( GetAlphaBuckets().end(), other->GetAlphaBuckets().begin(), other->GetAlphaBuckets().end() );
	other->GetAlphaBuckets().clear();
	//printf("AFTER %d %d\n", GetSolidBuckets().size(), GetAlphaBuckets().size());
}

