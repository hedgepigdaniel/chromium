// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the GLES2DecoderPassthroughImpl class.

#ifndef GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_DECODER_PASSTHROUGH_H_
#define GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_DECODER_PASSTHROUGH_H_

#include "base/memory/ref_counted.h"
#include "gpu/command_buffer/common/debug_marker_manager.h"
#include "gpu/command_buffer/common/gles2_cmd_format.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/client_service_map.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/image_manager.h"
#include "gpu/command_buffer/service/logger.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_surface.h"

namespace gpu {
namespace gles2 {

class ContextGroup;

struct MappedBuffer {
  GLsizeiptr size;
  GLbitfield access;
  uint8_t* map_ptr;
  int32_t data_shm_id;
  uint32_t data_shm_offset;
};

struct PassthroughResources {
  PassthroughResources();
  ~PassthroughResources();

  void Destroy(bool have_context);

  // Mappings from client side IDs to service side IDs.
  ClientServiceMap<GLuint, GLuint> texture_id_map;
  ClientServiceMap<GLuint, GLuint> buffer_id_map;
  ClientServiceMap<GLuint, GLuint> renderbuffer_id_map;
  ClientServiceMap<GLuint, GLuint> sampler_id_map;
  ClientServiceMap<GLuint, GLuint> program_id_map;
  ClientServiceMap<GLuint, GLuint> shader_id_map;

  static_assert(sizeof(uintptr_t) == sizeof(GLsync),
                "GLsync not the same size as uintptr_t");
  ClientServiceMap<GLuint, uintptr_t> sync_id_map;

  // Mapping of client texture IDs to TexturePassthrough objects used to make
  // sure all textures used by mailboxes are not deleted until all textures
  // using the mailbox are deleted
  std::unordered_map<GLuint, scoped_refptr<TexturePassthrough>>
      texture_object_map;

  // Mapping of client buffer IDs that are mapped to the shared memory used to
  // back the mapping so that it can be flushed when the buffer is unmapped
  std::unordered_map<GLuint, MappedBuffer> mapped_buffer_map;
};

class GLES2DecoderPassthroughImpl : public GLES2Decoder {
 public:
  explicit GLES2DecoderPassthroughImpl(ContextGroup* group);
  ~GLES2DecoderPassthroughImpl() override;

  Error DoCommands(unsigned int num_commands,
                   const volatile void* buffer,
                   int num_entries,
                   int* entries_processed) override;

  const char* GetCommandName(unsigned int command_id) const override;

  bool Initialize(const scoped_refptr<gl::GLSurface>& surface,
                  const scoped_refptr<gl::GLContext>& context,
                  bool offscreen,
                  const DisallowedFeatures& disallowed_features,
                  const ContextCreationAttribHelper& attrib_helper) override;

  // Destroys the graphics context.
  void Destroy(bool have_context) override;

  // Set the surface associated with the default FBO.
  void SetSurface(const scoped_refptr<gl::GLSurface>& surface) override;

  // Releases the surface associated with the GL context.
  // The decoder should not be used until a new surface is set.
  void ReleaseSurface() override;

  void TakeFrontBuffer(const Mailbox& mailbox) override;

  void ReturnFrontBuffer(const Mailbox& mailbox, bool is_lost) override;

  // Resize an offscreen frame buffer.
  bool ResizeOffscreenFramebuffer(const gfx::Size& size) override;

  // Make this decoder's GL context current.
  bool MakeCurrent() override;

  // Gets the GLES2 Util which holds info.
  GLES2Util* GetGLES2Util() override;

  // Gets the associated GLContext.
  gl::GLContext* GetGLContext() override;

  // Gets the associated ContextGroup
  ContextGroup* GetContextGroup() override;

  const FeatureInfo* GetFeatureInfo() const override;

  Capabilities GetCapabilities() override;

  // Restores all of the decoder GL state.
  void RestoreState(const ContextState* prev_state) override;

  // Restore States.
  void RestoreActiveTexture() const override;
  void RestoreAllTextureUnitAndSamplerBindings(
      const ContextState* prev_state) const override;
  void RestoreActiveTextureUnitBinding(unsigned int target) const override;
  void RestoreBufferBinding(unsigned int target) override;
  void RestoreBufferBindings() const override;
  void RestoreFramebufferBindings() const override;
  void RestoreRenderbufferBindings() override;
  void RestoreGlobalState() const override;
  void RestoreProgramBindings() const override;
  void RestoreTextureState(unsigned service_id) const override;
  void RestoreTextureUnitBindings(unsigned unit) const override;
  void RestoreVertexAttribArray(unsigned index) override;
  void RestoreAllExternalTextureBindingsIfNeeded() override;

  void ClearAllAttributes() const override;
  void RestoreAllAttributes() const override;

  void SetIgnoreCachedStateForTest(bool ignore) override;
  void SetForceShaderNameHashingForTest(bool force) override;
  size_t GetSavedBackTextureCountForTest() override;
  size_t GetCreatedBackTextureCountForTest() override;

  // Sets the callback for fence sync release and wait calls. The wait call
  // returns true if the channel is still scheduled.
  void SetFenceSyncReleaseCallback(
      const FenceSyncReleaseCallback& callback) override;
  void SetWaitSyncTokenCallback(const WaitSyncTokenCallback& callback) override;
  void SetDescheduleUntilFinishedCallback(
      const NoParamCallback& callback) override;
  void SetRescheduleAfterFinishedCallback(
      const NoParamCallback& callback) override;

  // Gets the QueryManager for this context.
  QueryManager* GetQueryManager() override;

  // Gets the FramebufferManager for this context.
  FramebufferManager* GetFramebufferManager() override;

  // Gets the TransformFeedbackManager for this context.
  TransformFeedbackManager* GetTransformFeedbackManager() override;

  // Gets the VertexArrayManager for this context.
  VertexArrayManager* GetVertexArrayManager() override;

  // Gets the ImageManager for this context.
  ImageManager* GetImageManager() override;

  // Returns false if there are no pending queries.
  bool HasPendingQueries() const override;

  // Process any pending queries.
  void ProcessPendingQueries(bool did_finish) override;

  // Returns false if there is no idle work to be made.
  bool HasMoreIdleWork() const override;

  // Perform any idle work that needs to be made.
  void PerformIdleWork() override;

  bool HasPollingWork() const override;
  void PerformPollingWork() override;

  bool GetServiceTextureId(uint32_t client_texture_id,
                           uint32_t* service_texture_id) override;

  // Provides detail about a lost context if one occurred.
  error::ContextLostReason GetContextLostReason() override;

  // Clears a level sub area of a texture
  // Returns false if a GL error should be generated.
  bool ClearLevel(Texture* texture,
                  unsigned target,
                  int level,
                  unsigned format,
                  unsigned type,
                  int xoffset,
                  int yoffset,
                  int width,
                  int height) override;

  // Clears a level sub area of a compressed 2D texture.
  // Returns false if a GL error should be generated.
  bool ClearCompressedTextureLevel(Texture* texture,
                                   unsigned target,
                                   int level,
                                   unsigned format,
                                   int width,
                                   int height) override;

  // Indicates whether a given internal format is one for a compressed
  // texture.
  bool IsCompressedTextureFormat(unsigned format) override;

  // Clears a level of a 3D texture.
  // Returns false if a GL error should be generated.
  bool ClearLevel3D(Texture* texture,
                    unsigned target,
                    int level,
                    unsigned format,
                    unsigned type,
                    int width,
                    int height,
                    int depth) override;

  ErrorState* GetErrorState() override;

  void SetShaderCacheCallback(const ShaderCacheCallback& callback) override;

  void WaitForReadPixels(base::Closure callback) override;

  // Returns true if the context was lost either by GL_ARB_robustness, forced
  // context loss or command buffer parse error.
  bool WasContextLost() const override;

  // Returns true if the context was lost specifically by GL_ARB_robustness.
  bool WasContextLostByRobustnessExtension() const override;

  // Lose this context.
  void MarkContextLost(error::ContextLostReason reason) override;

  Logger* GetLogger() override;

  const ContextState* GetContextState() override;
  scoped_refptr<ShaderTranslatorInterface> GetTranslator(GLenum type) override;

 private:
  void* GetScratchMemory(size_t size);

  template <typename T>
  T* GetTypedScratchMemory(size_t count) {
    return reinterpret_cast<T*>(GetScratchMemory(count * sizeof(T)));
  }

  template <typename T, typename GLGetFunction>
  error::Error GetNumericHelper(GLenum pname,
                                GLsizei bufsize,
                                GLsizei* length,
                                T* params,
                                GLGetFunction get_call) {
    // Get a scratch buffer to hold the result of the query
    T* scratch_params = GetTypedScratchMemory<T>(bufsize);
    get_call(pname, bufsize, length, scratch_params);

    // Update the results of the query, if needed
    error::Error error = PatchGetNumericResults(pname, *length, scratch_params);
    if (error != error::kNoError) {
      *length = 0;
      return error;
    }

    // Copy into the destination
    DCHECK(*length < bufsize);
    std::copy(scratch_params, scratch_params + *length, params);

    return error::kNoError;
  }

  template <typename T>
  error::Error PatchGetNumericResults(GLenum pname, GLsizei length, T* params);
  error::Error PatchGetFramebufferAttachmentParameter(GLenum target,
                                                      GLenum attachment,
                                                      GLenum pname,
                                                      GLsizei length,
                                                      GLint* params);

  void InsertError(GLenum error, const std::string& message);
  GLenum PopError();
  bool FlushErrors();

  bool IsEmulatedQueryTarget(GLenum target) const;
  error::Error ProcessQueries(bool did_finish);
  void RemovePendingQuery(GLuint service_id);

  void UpdateTextureBinding(GLenum target, GLuint client_id, GLuint service_id);

  error::Error BindTexImage2DCHROMIUMImpl(GLenum target,
                                          GLenum internalformat,
                                          GLint image_id);

  int commands_to_process_;

  DebugMarkerManager debug_marker_manager_;
  Logger logger_;

#define GLES2_CMD_OP(name) \
  Error Handle##name(uint32_t immediate_data_size, const volatile void* data);

  GLES2_COMMAND_LIST(GLES2_CMD_OP)
#undef GLES2_CMD_OP

  using CmdHandler =
      Error (GLES2DecoderPassthroughImpl::*)(uint32_t immediate_data_size,
                                             const volatile void* data);

  // A struct to hold info about each command.
  struct CommandInfo {
    CmdHandler cmd_handler;
    uint8_t arg_flags;   // How to handle the arguments for this scommand
    uint8_t cmd_flags;   // How to handle this command
    uint16_t arg_count;  // How many arguments are expected for this command.
  };

  // A table of CommandInfo for all the commands.
  static const CommandInfo command_info[kNumCommands - kFirstGLES2Command];

  // The GL context this decoder renders to on behalf of the client.
  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<gl::GLContext> context_;
  bool offscreen_;

  // Managers
  std::unique_ptr<ImageManager> image_manager_;

  // The ContextGroup for this decoder uses to track resources.
  scoped_refptr<ContextGroup> group_;
  scoped_refptr<FeatureInfo> feature_info_;

  // Callbacks
  FenceSyncReleaseCallback fence_sync_release_callback_;
  WaitSyncTokenCallback wait_sync_token_callback_;

  // Some objects may generate resources when they are bound even if they were
  // not generated yet: texture, buffer, renderbuffer, framebuffer, transform
  // feedback, vertex array
  bool bind_generates_resource_;

  // Mappings from client side IDs to service side IDs for shared objects
  PassthroughResources* resources_;

  // Mappings from client side IDs to service side IDs for per-context objects
  ClientServiceMap<GLuint, GLuint> framebuffer_id_map_;
  ClientServiceMap<GLuint, GLuint> transform_feedback_id_map_;
  ClientServiceMap<GLuint, GLuint> query_id_map_;
  ClientServiceMap<GLuint, GLuint> vertex_array_id_map_;

  // Mailboxes
  scoped_refptr<MailboxManager> mailbox_manager_;

  // State tracking of currently bound 2D textures (client IDs)
  size_t active_texture_unit_;
  std::unordered_map<GLenum, std::vector<GLuint>> bound_textures_;

  // State tracking of currently bound buffers
  std::unordered_map<GLenum, GLuint> bound_buffers_;

  // Track the service-id to type of all queries for validation
  struct QueryInfo {
    GLenum type = GL_NONE;
  };
  std::unordered_map<GLuint, QueryInfo> query_info_map_;

  // All queries that are waiting for their results to be ready
  struct PendingQuery {
    GLenum target = GL_NONE;
    GLuint service_id = 0;

    int32_t shm_id = 0;
    uint32_t shm_offset = 0;
    base::subtle::Atomic32 submit_count = 0;
  };
  std::deque<PendingQuery> pending_queries_;

  // Currently active queries
  struct ActiveQuery {
    GLuint service_id = 0;
    int32_t shm_id = 0;
    uint32_t shm_offset = 0;
  };
  std::unordered_map<GLenum, ActiveQuery> active_queries_;

  std::set<GLenum> errors_;

  // Cache of scratch memory
  std::vector<uint8_t> scratch_memory_;

// Include the prototypes of all the doer functions from a separate header to
// keep this file clean.
#include "gpu/command_buffer/service/gles2_cmd_decoder_passthrough_doer_prototypes.h"
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_GLES2_CMD_DECODER_PASSTHROUGH_H_
