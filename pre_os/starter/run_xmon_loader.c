/*******************************************************************************
* Copyright (c) 2015 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "mon_defs.h"
#include "mon_arch_defs.h"
#include "image_loader.h"
#include "xmon_desc.h"

int run_xmon_loader(xmon_desc_t *td)
{
	boolean_t ok;
	void *img;

	void (*xmon_loader) (xmon_desc_t *td);

	img = (void *)((uint32_t)td + td->xmon_loader_start * 512);

	ok = load_image((void *)img,
		(void *)XMON_LOADER_BASE(
			td), XMON_LOADER_SIZE, (uint64_t *)&xmon_loader);

	if (!ok) {
		return -1;
	}

	xmon_loader(td);

	return -1;
}

/* End of file */
