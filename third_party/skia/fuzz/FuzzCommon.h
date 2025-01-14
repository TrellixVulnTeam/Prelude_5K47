/*
 * Copyright 2018 Google, LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FuzzCommon_DEFINED
#define FuzzCommon_DEFINED

#include "Fuzz.h"
#include "SkMatrix.h"
#include "SkPath.h"
#include "SkRRect.h"
#include "SkRegion.h"

// allows some float values for path points
void FuzzPath(Fuzz* fuzz, SkPath* path, int maxOps);
// allows all float values for path points
void BuildPath(Fuzz* fuzz, SkPath* path, int last_verb);

void FuzzNiceRRect(Fuzz* fuzz, SkRRect* rr);

void FuzzNiceMatrix(Fuzz* fuzz, SkMatrix* m);

void FuzzNiceRegion(Fuzz* fuzz, SkRegion* region, int maxN);

#endif

