// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the GLES2 command buffer commands.

#ifndef GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_FORMAT_H_
#define GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_FORMAT_H_


#include <KHR/khrplatform.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/atomicops.h"
#include "base/logging.h"
#include "base/macros.h"
#include "gpu/command_buffer/common/bitfield_helpers.h"
#include "gpu/command_buffer/common/cmd_buffer_common.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gles2_cmd_ids.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"

// GL types are forward declared to avoid including the GL headers. The problem
// is determining which GL headers to include from code that is common to the
// client and service sides (GLES2 or one of several GL implementations).
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long GLulong;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef khronos_intptr_t GLintptr;
typedef khronos_ssize_t  GLsizeiptr;
typedef struct __GLsync *GLsync;
typedef int64_t GLint64;
typedef uint64_t GLuint64;

namespace gpu {
namespace gles2 {

// Command buffer is GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT byte aligned.
#pragma pack(push, 4)
static_assert(GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT == 4,
              "pragma pack alignment must be equal to "
              "GPU_COMMAND_BUFFER_ENTRY_ALIGNMENT");

namespace id_namespaces {

// These are used when contexts share resources.
enum class SharedIdNamespaces {
  kBuffers,
  kProgramsAndShaders,
  kRenderbuffers,
  kTextures,
  kSamplers,
  kSyncs,
  kNumSharedIdNamespaces
};

enum class IdNamespaces {
  kFramebuffers,
  kQueries,
  kVertexArrays,
  kTransformFeedbacks,
  kNumIdNamespaces
};

enum RangeIdNamespaces { kPaths, kNumRangeIdNamespaces };

// These numbers must not change
static_assert(static_cast<int>(SharedIdNamespaces::kBuffers) == 0,
              "kBuffers should equal 0");
static_assert(static_cast<int>(SharedIdNamespaces::kProgramsAndShaders) == 1,
              "kProgramsAndShaders should equal 1");
static_assert(static_cast<int>(SharedIdNamespaces::kRenderbuffers) == 2,
              "kRenderbuffers should equal 2");
static_assert(static_cast<int>(SharedIdNamespaces::kTextures) == 3,
              "kTextures should equal 3");
static_assert(static_cast<int>(SharedIdNamespaces::kSamplers) == 4,
              "kSamplers should equal 4");
static_assert(static_cast<int>(SharedIdNamespaces::kSyncs) == 5,
              "kProgramsAndShaders should equal 5");
static_assert(static_cast<int>(IdNamespaces::kFramebuffers) == 0,
              "kFramebuffers should equal 0");
static_assert(static_cast<int>(IdNamespaces::kQueries) == 1,
              "kQueries should equal 1");
static_assert(static_cast<int>(IdNamespaces::kVertexArrays) == 2,
              "kVertexArrays should equal 2");
static_assert(static_cast<int>(IdNamespaces::kTransformFeedbacks) == 3,
              "kTransformFeedbacks should equal 3");
static_assert(kPaths == 0, "kPaths should equal 0");

}  // namespace id_namespaces

// Used for some glGetXXX commands that return a result through a pointer. We
// need to know if the command succeeded or not and the size of the result. If
// the command failed its result size will 0.
template <typename T>
struct SizedResult {
  typedef T Type;

  T* GetData() {
    return static_cast<T*>(static_cast<void*>(&data));
  }

  // Returns the total size in bytes of the SizedResult for a given number of
  // results including the size field.
  static size_t ComputeSize(size_t num_results) {
    return sizeof(T) * num_results + sizeof(uint32_t);  // NOLINT
  }

  // Returns the maximum number of results for a given buffer size.
  static uint32_t ComputeMaxResults(size_t size_of_buffer) {
    return (size_of_buffer >= sizeof(uint32_t)) ?
        ((size_of_buffer - sizeof(uint32_t)) / sizeof(T)) : 0;  // NOLINT
  }

  // Set the size for a given number of results.
  void SetNumResults(size_t num_results) {
    size = sizeof(T) * num_results;  // NOLINT
  }

  // Get the number of elements in the result
  int32_t GetNumResults() const {
    return size / sizeof(T);  // NOLINT
  }

  // Copy the result.
  void CopyResult(void* dst) const {
    memcpy(dst, &data, size);
  }

  uint32_t size;  // in bytes.
  int32_t data;  // this is just here to get an offset.
};

static_assert(sizeof(SizedResult<int8_t>) == 8,
              "size of SizedResult<int8_t> should be 8");
static_assert(offsetof(SizedResult<int8_t>, size) == 0,
              "offset of SizedResult<int8_t>.size should be 0");
static_assert(offsetof(SizedResult<int8_t>, data) == 4,
              "offset of SizedResult<int8_t>.data should be 4");

// The data for one attrib or uniform from GetProgramInfoCHROMIUM.
struct ProgramInput {
  uint32_t type;             // The type (GL_VEC3, GL_MAT3, GL_SAMPLER_2D, etc.
  int32_t size;              // The size (how big the array is for uniforms)
  uint32_t location_offset;  // offset from ProgramInfoHeader to 'size'
                             // locations for uniforms, 1 for attribs.
  uint32_t name_offset;      // offset from ProgrmaInfoHeader to start of name.
  uint32_t name_length;      // length of the name.
};

// The format of the bucket filled out by GetProgramInfoCHROMIUM
struct ProgramInfoHeader {
  uint32_t link_status;
  uint32_t num_attribs;
  uint32_t num_uniforms;
  // ProgramInput inputs[num_attribs + num_uniforms];
};

// The data for one UniformBlock from GetProgramInfoCHROMIUM
struct UniformBlockInfo {
  uint32_t binding;  // UNIFORM_BLOCK_BINDING
  uint32_t data_size;  // UNIFORM_BLOCK_DATA_SIZE
  uint32_t name_offset;  // offset from UniformBlocksHeader to start of name.
  uint32_t name_length;  // UNIFORM_BLOCK_NAME_LENGTH
  uint32_t active_uniforms;  // UNIFORM_BLOCK_ACTIVE_UNIFORMS
  // offset from UniformBlocksHeader to |active_uniforms| indices.
  uint32_t active_uniform_offset;
  // UNIFORM_BLOCK_REFERENDED_BY_VERTEX_SHADER
  uint32_t referenced_by_vertex_shader;
  // UNIFORM_BLOCK_REFERENDED_BY_FRAGMENT_SHADER
  uint32_t referenced_by_fragment_shader;
};

// The format of the bucket filled out by GetUniformBlocksCHROMIUM
struct UniformBlocksHeader {
  uint32_t num_uniform_blocks;
  // UniformBlockInfo uniform_blocks[num_uniform_blocks];
};

// The data for one TransformFeedbackVarying from
// GetTransformFeedbackVaringCHROMIUM.
struct TransformFeedbackVaryingInfo {
  uint32_t size;
  uint32_t type;
  uint32_t name_offset;  // offset from Header to start of name.
  uint32_t name_length;  // including the null terminator.
};

// The format of the bucket filled out by GetTransformFeedbackVaryingsCHROMIUM
struct TransformFeedbackVaryingsHeader {
  uint32_t transform_feedback_buffer_mode;
  uint32_t num_transform_feedback_varyings;
  // TransformFeedbackVaryingInfo varyings[num_transform_feedback_varyings];
};

// Parameters of a uniform that can be queried through glGetActiveUniformsiv,
// but not through glGetActiveUniform.
struct UniformES3Info {
  int32_t block_index;
  int32_t offset;
  int32_t array_stride;
  int32_t matrix_stride;
  int32_t is_row_major;
};

// The format of the bucket filled out by GetUniformsivES3CHROMIUM
struct UniformsES3Header {
  uint32_t num_uniforms;
  // UniformES3Info uniforms[num_uniforms];
};

// The format of QuerySync used by EXT_occlusion_query_boolean
struct QuerySync {
  void Reset() {
    process_count = 0;
    result = 0;
  }

  base::subtle::Atomic32 process_count;
  uint64_t result;
};

struct DisjointValueSync {
  void Reset() {
    base::subtle::Release_Store(&disjoint_count, 0);
  }

  void SetDisjointCount(uint32_t token) {
    DCHECK_NE(token, 0u);
    base::subtle::Release_Store(&disjoint_count, token);
  }

  uint32_t GetDisjointCount() {
    return base::subtle::Acquire_Load(&disjoint_count);
  }

  base::subtle::Atomic32 disjoint_count;
};

static_assert(sizeof(QuerySync) == 12, "size of QuerySync should be 12");
static_assert(offsetof(QuerySync, process_count) == 0,
              "offset of QuerySync.process_count should be 0");
static_assert(offsetof(QuerySync, result) == 4,
              "offset of QuerySync.result should be 4");

static_assert(sizeof(ProgramInput) == 20, "size of ProgramInput should be 20");
static_assert(offsetof(ProgramInput, type) == 0,
              "offset of ProgramInput.type should be 0");
static_assert(offsetof(ProgramInput, size) == 4,
              "offset of ProgramInput.size should be 4");
static_assert(offsetof(ProgramInput, location_offset) == 8,
              "offset of ProgramInput.location_offset should be 8");
static_assert(offsetof(ProgramInput, name_offset) == 12,
              "offset of ProgramInput.name_offset should be 12");
static_assert(offsetof(ProgramInput, name_length) == 16,
              "offset of ProgramInput.name_length should be 16");

static_assert(sizeof(ProgramInfoHeader) == 12,
              "size of ProgramInfoHeader should be 12");
static_assert(offsetof(ProgramInfoHeader, link_status) == 0,
              "offset of ProgramInfoHeader.link_status should be 0");
static_assert(offsetof(ProgramInfoHeader, num_attribs) == 4,
              "offset of ProgramInfoHeader.num_attribs should be 4");
static_assert(offsetof(ProgramInfoHeader, num_uniforms) == 8,
              "offset of ProgramInfoHeader.num_uniforms should be 8");

static_assert(sizeof(UniformBlockInfo) == 32,
              "size of UniformBlockInfo should be 32");
static_assert(offsetof(UniformBlockInfo, binding) == 0,
              "offset of UniformBlockInfo.binding should be 0");
static_assert(offsetof(UniformBlockInfo, data_size) == 4,
              "offset of UniformBlockInfo.data_size should be 4");
static_assert(offsetof(UniformBlockInfo, name_offset) == 8,
              "offset of UniformBlockInfo.name_offset should be 8");
static_assert(offsetof(UniformBlockInfo, name_length) == 12,
              "offset of UniformBlockInfo.name_length should be 12");
static_assert(offsetof(UniformBlockInfo, active_uniforms) == 16,
              "offset of UniformBlockInfo.active_uniforms should be 16");
static_assert(offsetof(UniformBlockInfo, active_uniform_offset) == 20,
              "offset of UniformBlockInfo.active_uniform_offset should be 20");
static_assert(offsetof(UniformBlockInfo, referenced_by_vertex_shader) == 24,
              "offset of UniformBlockInfo.referenced_by_vertex_shader "
              "should be 24");
static_assert(offsetof(UniformBlockInfo, referenced_by_fragment_shader) == 28,
              "offset of UniformBlockInfo.referenced_by_fragment_shader "
              "should be 28");

static_assert(sizeof(UniformBlocksHeader) == 4,
              "size of UniformBlocksHeader should be 4");
static_assert(offsetof(UniformBlocksHeader, num_uniform_blocks) == 0,
              "offset of UniformBlocksHeader.num_uniform_blocks should be 0");

namespace cmds {

#include "../common/gles2_cmd_format_autogen.h"

#pragma pack(pop)

}  // namespace cmd
}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_FORMAT_H_
