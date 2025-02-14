//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// StateChangeTest:
//   Specifically designed for an ANGLE implementation of GL, these tests validate that
//   ANGLE's dirty bits systems don't get confused by certain sequences of state changes.
//

#include "test_utils/ANGLETest.h"
#include "test_utils/gl_raii.h"

using namespace angle;

namespace
{

class StateChangeTest : public ANGLETest
{
  protected:
    StateChangeTest()
    {
        setWindowWidth(64);
        setWindowHeight(64);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);

        // Enable the no error extension to avoid syncing the FBO state on validation.
        setNoErrorEnabled(true);
    }

    void SetUp() override
    {
        ANGLETest::SetUp();

        glGenFramebuffers(1, &mFramebuffer);
        glGenTextures(2, mTextures.data());
        glGenRenderbuffers(1, &mRenderbuffer);

        ASSERT_GL_NO_ERROR();
    }

    void TearDown() override
    {
        if (mFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &mFramebuffer);
            mFramebuffer = 0;
        }

        if (!mTextures.empty())
        {
            glDeleteTextures(static_cast<GLsizei>(mTextures.size()), mTextures.data());
            mTextures.clear();
        }

        glDeleteRenderbuffers(1, &mRenderbuffer);

        ANGLETest::TearDown();
    }

    GLuint mFramebuffer           = 0;
    GLuint mRenderbuffer          = 0;
    std::vector<GLuint> mTextures = {0, 0};
};

class StateChangeTestES3 : public StateChangeTest
{
  protected:
    StateChangeTestES3() {}
};

// Ensure that CopyTexImage2D syncs framebuffer changes.
TEST_P(StateChangeTest, CopyTexImage2DSync)
{
    // TODO(geofflang): Fix on Linux AMD drivers (http://anglebug.com/1291)
    ANGLE_SKIP_TEST_IF(IsAMD() && IsOpenGL());

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    // Init first texture to red
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    // Init second texture to green
    glBindTexture(GL_TEXTURE_2D, mTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[1], 0);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);

    // Copy in the red texture to the green one.
    // CopyTexImage should sync the framebuffer attachment change.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 16, 16, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[1], 0);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    ASSERT_GL_NO_ERROR();
}

// Ensure that CopyTexSubImage2D syncs framebuffer changes.
TEST_P(StateChangeTest, CopyTexSubImage2DSync)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    // Init first texture to red
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    // Init second texture to green
    glBindTexture(GL_TEXTURE_2D, mTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[1], 0);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);

    // Copy in the red texture to the green one.
    // CopyTexImage should sync the framebuffer attachment change.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 16, 16);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[1], 0);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    ASSERT_GL_NO_ERROR();
}

// Test that Framebuffer completeness caching works when color attachments change.
TEST_P(StateChangeTest, FramebufferIncompleteColorAttachment)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture at color attachment 0 to be non-color-renderable.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 16, 16, 0, GL_ALPHA, GL_UNSIGNED_BYTE, nullptr);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that caching works when color attachments change with TexStorage.
TEST_P(StateChangeTest, FramebufferIncompleteWithTexStorage)
{
    ANGLE_SKIP_TEST_IF(!extensionEnabled("GL_EXT_texture_storage"));

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture at color attachment 0 to be non-color-renderable.
    glTexStorage2DEXT(GL_TEXTURE_2D, 1, GL_ALPHA8_EXT, 16, 16);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that caching works when color attachments change with CompressedTexImage2D.
TEST_P(StateChangeTestES3, FramebufferIncompleteWithCompressedTex)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture at color attachment 0 to be non-color-renderable.
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2, 16, 16, 0, 128, nullptr);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that caching works when color attachments are deleted.
TEST_P(StateChangeTestES3, FramebufferIncompleteWhenAttachmentDeleted)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Delete the texture at color attachment 0.
    glDeleteTextures(1, &mTextures[0]);
    mTextures[0] = 0;
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that Framebuffer completeness caching works when depth attachments change.
TEST_P(StateChangeTest, FramebufferIncompleteDepthAttachment)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 16, 16);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mRenderbuffer);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture at color attachment 0 to be non-depth-renderable.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that Framebuffer completeness caching works when stencil attachments change.
TEST_P(StateChangeTest, FramebufferIncompleteStencilAttachment)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, 16, 16);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              mRenderbuffer);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture at the stencil attachment to be non-stencil-renderable.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that Framebuffer completeness caching works when depth-stencil attachments change.
TEST_P(StateChangeTest, FramebufferIncompleteDepthStencilAttachment)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3 &&
                       !extensionEnabled("GL_OES_packed_depth_stencil"));

    // TODO(jmadill): Investigate the failure (https://anglebug.com/1388)
    ANGLE_SKIP_TEST_IF(IsWindows() && IsIntel() && IsOpenGL());

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 16, 16);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              mRenderbuffer);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Change the texture the depth-stencil attachment to be non-depth-stencil-renderable.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

const char kSimpleAttributeVS[] = R"(attribute vec2 position;
attribute vec4 testAttrib;
varying vec4 testVarying;
void main()
{
    gl_Position = vec4(position, 0, 1);
    testVarying = testAttrib;
})";

const char kSimpleAttributeFS[] = R"(precision mediump float;
varying vec4 testVarying;
void main()
{
    gl_FragColor = testVarying;
})";

// Tests that using a buffered attribute, then disabling it and using current value, works.
TEST_P(StateChangeTest, DisablingBufferedVertexAttribute)
{
    ANGLE_GL_PROGRAM(program, kSimpleAttributeVS, kSimpleAttributeFS);
    glUseProgram(program);
    GLint attribLoc   = glGetAttribLocation(program, "testAttrib");
    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, attribLoc);
    ASSERT_NE(-1, positionLoc);

    // Set up the buffered attribute.
    std::vector<GLColor> red(6, GLColor::red);
    GLBuffer attribBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, attribBuffer);
    glBufferData(GL_ARRAY_BUFFER, red.size() * sizeof(GLColor), red.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(attribLoc);
    glVertexAttribPointer(attribLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);

    // Also set the current value to green now.
    glVertexAttrib4f(attribLoc, 0.0f, 1.0f, 0.0f, 1.0f);

    // Set up the position attribute as well.
    setupQuadVertexBuffer(0.5f, 1.0f);
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Draw with the buffered attribute. Verify red.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Draw with the disabled "current value attribute". Verify green.
    glDisableVertexAttribArray(attribLoc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Verify setting buffer data on the disabled buffer doesn't change anything.
    std::vector<GLColor> blue(128, GLColor::blue);
    glBindBuffer(GL_ARRAY_BUFFER, attribBuffer);
    glBufferData(GL_ARRAY_BUFFER, blue.size() * sizeof(GLColor), blue.data(), GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Ensure that CopyTexSubImage3D syncs framebuffer changes.
TEST_P(StateChangeTestES3, CopyTexSubImage3DSync)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    // Init first texture to red
    glBindTexture(GL_TEXTURE_3D, mTextures[0]);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, 16, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTextures[0], 0, 0);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    // Init second texture to green
    glBindTexture(GL_TEXTURE_3D, mTextures[1]);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, 16, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTextures[1], 0, 0);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);

    // Copy in the red texture to the green one.
    // CopyTexImage should sync the framebuffer attachment change.
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTextures[0], 0, 0);
    glCopyTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 0, 0, 16, 16);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTextures[1], 0, 0);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    ASSERT_GL_NO_ERROR();
}

// Ensure that BlitFramebuffer syncs framebuffer changes.
TEST_P(StateChangeTestES3, BlitFramebufferSync)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    // Init first texture to red
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    // Init second texture to green
    glBindTexture(GL_TEXTURE_2D, mTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[1], 0);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);

    // Change to the red textures and blit.
    // BlitFramebuffer should sync the framebuffer attachment change.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0],
                           0);
    glBlitFramebuffer(0, 0, 16, 16, 0, 0, 16, 16, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    ASSERT_GL_NO_ERROR();
}

// Ensure that ReadBuffer and DrawBuffers sync framebuffer changes.
TEST_P(StateChangeTestES3, ReadBufferAndDrawBuffersSync)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    // Initialize two FBO attachments
    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);
    glBindTexture(GL_TEXTURE_2D, mTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mTextures[1], 0);

    // Clear first attachment to red
    GLenum bufs1[] = {GL_COLOR_ATTACHMENT0, GL_NONE};
    glDrawBuffers(2, bufs1);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Clear second texture to green
    GLenum bufs2[] = {GL_NONE, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs2);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Verify first attachment is red and second is green
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);

    ASSERT_GL_NO_ERROR();
}

// Tests calling invalidate on incomplete framebuffers after switching attachments.
// Adapted partially from WebGL 2 test "renderbuffers/invalidate-framebuffer"
TEST_P(StateChangeTestES3, IncompleteRenderbufferAttachmentInvalidateSync)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
    GLint samples = 0;
    glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA8, GL_SAMPLES, 1, &samples);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mRenderbuffer);
    ASSERT_GL_NO_ERROR();

    // invalidate the framebuffer when the attachment is incomplete: no storage allocated to the
    // attached renderbuffer
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));
    GLenum attachments1[] = {GL_COLOR_ATTACHMENT0};
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, attachments1);
    ASSERT_GL_NO_ERROR();

    glRenderbufferStorageMultisample(GL_RENDERBUFFER, static_cast<GLsizei>(samples), GL_RGBA8,
                                     getWindowWidth(), getWindowHeight());
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glClear(GL_COLOR_BUFFER_BIT);
    ASSERT_GL_NO_ERROR();

    GLRenderbuffer renderbuf;

    glBindRenderbuffer(GL_RENDERBUFFER, renderbuf.get());
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              renderbuf.get());
    ASSERT_GL_NO_ERROR();

    // invalidate the framebuffer when the attachment is incomplete: no storage allocated to the
    // attached renderbuffer
    // Note: the bug will only repro *without* a call to checkStatus before the invalidate.
    GLenum attachments2[] = {GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, attachments2);

    glRenderbufferStorageMultisample(GL_RENDERBUFFER, static_cast<GLsizei>(samples),
                                     GL_DEPTH_COMPONENT16, getWindowWidth(), getWindowHeight());
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glClear(GL_DEPTH_BUFFER_BIT);
    ASSERT_GL_NO_ERROR();
}

class StateChangeRenderTest : public StateChangeTest
{
  protected:
    StateChangeRenderTest() : mProgram(0), mRenderbuffer(0) {}

    void SetUp() override
    {
        StateChangeTest::SetUp();

        const std::string vertexShaderSource =
            "attribute vec2 position;\n"
            "void main() {\n"
            "    gl_Position = vec4(position, 0, 1);\n"
            "}";
        const std::string fragmentShaderSource =
            "uniform highp vec4 uniformColor;\n"
            "void main() {\n"
            "    gl_FragColor = uniformColor;\n"
            "}";

        mProgram = CompileProgram(vertexShaderSource, fragmentShaderSource);
        ASSERT_NE(0u, mProgram);

        glGenRenderbuffers(1, &mRenderbuffer);
    }

    void TearDown() override
    {
        glDeleteProgram(mProgram);
        glDeleteRenderbuffers(1, &mRenderbuffer);

        StateChangeTest::TearDown();
    }

    void setUniformColor(const GLColor &color)
    {
        glUseProgram(mProgram);
        const Vector4 &normalizedColor = color.toNormalizedVector();
        GLint uniformLocation          = glGetUniformLocation(mProgram, "uniformColor");
        ASSERT_NE(-1, uniformLocation);
        glUniform4fv(uniformLocation, 1, normalizedColor.data());
    }

    GLuint mProgram;
    GLuint mRenderbuffer;
};

// Test that re-creating a currently attached texture works as expected.
TEST_P(StateChangeRenderTest, RecreateTexture)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Draw with red to the FBO.
    GLColor red(255, 0, 0, 255);
    setUniformColor(red);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, red);

    // Recreate the texture with green.
    GLColor green(0, 255, 0, 255);
    std::vector<GLColor> greenPixels(32 * 32, green);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 greenPixels.data());
    EXPECT_PIXEL_COLOR_EQ(0, 0, green);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Verify drawing blue gives blue. This covers the FBO sync with D3D dirty bits.
    GLColor blue(0, 0, 255, 255);
    setUniformColor(blue);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, blue);

    EXPECT_GL_NO_ERROR();
}

// Test that re-creating a currently attached renderbuffer works as expected.
TEST_P(StateChangeRenderTest, RecreateRenderbuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mRenderbuffer);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Draw with red to the FBO.
    GLColor red(255, 0, 0, 255);
    setUniformColor(red);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, red);

    // Recreate the renderbuffer and clear to green.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 32, 32);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLColor green(0, 255, 0, 255);
    EXPECT_PIXEL_COLOR_EQ(0, 0, green);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Verify drawing blue gives blue. This covers the FBO sync with D3D dirty bits.
    GLColor blue(0, 0, 255, 255);
    setUniformColor(blue);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, blue);

    EXPECT_GL_NO_ERROR();
}

// Test that recreating a texture with GenerateMipmaps signals the FBO is dirty.
TEST_P(StateChangeRenderTest, GenerateMipmap)
{
    // TODO(lucferron): Diagnose and fix
    // http://anglebug.com/2652
    ANGLE_SKIP_TEST_IF(IsVulkan());

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

    glBindTexture(GL_TEXTURE_2D, mTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextures[0], 0);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Draw once to set the RenderTarget in D3D11
    GLColor red(255, 0, 0, 255);
    setUniformColor(red);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, red);

    // This will trigger the texture to be re-created on FL9_3.
    glGenerateMipmap(GL_TEXTURE_2D);

    // Explictly check FBO status sync in some versions of ANGLE no_error skips FBO checks.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Now ensure we don't have a stale render target.
    GLColor blue(0, 0, 255, 255);
    setUniformColor(blue);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, blue);

    EXPECT_GL_NO_ERROR();
}

// Tests that D3D11 dirty bit updates don't forget about BufferSubData attrib updates.
TEST_P(StateChangeTest, VertexBufferUpdatedAfterDraw)
{
    // TODO(jie.a.chen@intel.com): Re-enable the test once the driver fix is
    // available in public release.
    // http://anglebug.com/2664.
    ANGLE_SKIP_TEST_IF(IsVulkan() && IsIntel());

    const std::string vs =
        "attribute vec2 position;\n"
        "attribute vec4 color;\n"
        "varying vec4 outcolor;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = vec4(position, 0, 1);\n"
        "    outcolor = color;\n"
        "}";
    const std::string fs =
        "varying mediump vec4 outcolor;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = outcolor;\n"
        "}";

    ANGLE_GL_PROGRAM(program, vs, fs);
    glUseProgram(program);

    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);
    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLoc);

    setupQuadVertexBuffer(0.5f, 1.0f);
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    GLBuffer colorBuf;
    glBindBuffer(GL_ARRAY_BUFFER, colorBuf);
    glVertexAttribPointer(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glEnableVertexAttribArray(colorLoc);

    // Fill with green.
    std::vector<GLColor> colorData(6, GLColor::green);
    glBufferData(GL_ARRAY_BUFFER, colorData.size() * sizeof(GLColor), colorData.data(),
                 GL_STATIC_DRAW);

    // Draw, expect green.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    ASSERT_GL_NO_ERROR();

    // Update buffer with red.
    std::fill(colorData.begin(), colorData.end(), GLColor::red);
    glBufferSubData(GL_ARRAY_BUFFER, 0, colorData.size() * sizeof(GLColor), colorData.data());

    // Draw, expect red.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    ASSERT_GL_NO_ERROR();
}

// Test that switching VAOs keeps the disabled "current value" attributes up-to-date.
TEST_P(StateChangeTestES3, VertexArrayObjectAndDisabledAttributes)
{
    const std::string singleVertexShader =
        "attribute vec4 position; void main() { gl_Position = position; }";
    const std::string singleFragmentShader = "void main() { gl_FragColor = vec4(1, 0, 0, 1); }";
    ANGLE_GL_PROGRAM(singleProgram, singleVertexShader, singleFragmentShader);

    const std::string dualVertexShader =
        "#version 300 es\n"
        "in vec4 position;\n"
        "in vec4 color;\n"
        "out vec4 varyColor;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = position;\n"
        "    varyColor = color;\n"
        "}";
    const std::string dualFragmentShader =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 varyColor;\n"
        "out vec4 colorOut;\n"
        "void main()\n"
        "{\n"
        "    colorOut = varyColor;\n"
        "}";
    ANGLE_GL_PROGRAM(dualProgram, dualVertexShader, dualFragmentShader);
    GLint positionLocation = glGetAttribLocation(dualProgram, "position");
    ASSERT_NE(-1, positionLocation);
    GLint colorLocation = glGetAttribLocation(dualProgram, "color");
    ASSERT_NE(-1, colorLocation);

    GLint singlePositionLocation = glGetAttribLocation(singleProgram, "position");
    ASSERT_NE(-1, singlePositionLocation);

    glUseProgram(singleProgram);

    // Initialize position vertex buffer.
    const auto &quadVertices = GetQuadVertices();

    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3) * 6, quadVertices.data(), GL_STATIC_DRAW);

    // Initialize a VAO. Draw with single program.
    GLVertexArray vertexArray;
    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(singlePositionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(singlePositionLocation);

    // Should draw red.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Draw with a green buffer attribute, without the VAO.
    glBindVertexArray(0);
    glUseProgram(dualProgram);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLocation);

    std::vector<GLColor> greenColors(6, GLColor::green);
    GLBuffer greenBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, greenBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLColor) * 6, greenColors.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(colorLocation, 4, GL_UNSIGNED_BYTE, GL_FALSE, 4, nullptr);
    glEnableVertexAttribArray(colorLocation);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Re-bind VAO and try to draw with different program, without changing state.
    // Should draw black since current value is not initialized.
    glBindVertexArray(vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::black);
}

const char kSamplerMetadataVertexShader0[] = R"(#version 300 es
precision mediump float;
out vec4 color;
uniform sampler2D texture;
void main()
{
    vec2 size = vec2(textureSize(texture, 0));
    color = size.x != 0.0 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 0.0);
    vec2 pos = vec2(0.0);
    switch (gl_VertexID) {
        case 0: pos = vec2(-1.0, -1.0); break;
        case 1: pos = vec2(3.0, -1.0); break;
        case 2: pos = vec2(-1.0, 3.0); break;
    };
    gl_Position = vec4(pos, 0.0, 1.0);
})";

const char kSamplerMetadataVertexShader1[] = R"(#version 300 es
precision mediump float;
out vec4 color;
uniform sampler2D texture1;
uniform sampler2D texture2;
void main()
{
    vec2 size1 = vec2(textureSize(texture1, 0));
    vec2 size2 = vec2(textureSize(texture2, 0));
    color = size1.x * size2.x != 0.0 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 0.0);
    vec2 pos = vec2(0.0);
    switch (gl_VertexID) {
        case 0: pos = vec2(-1.0, -1.0); break;
        case 1: pos = vec2(3.0, -1.0); break;
        case 2: pos = vec2(-1.0, 3.0); break;
    };
    gl_Position = vec4(pos, 0.0, 1.0);
})";

const char kSamplerMetadataFragmentShader[] = R"(#version 300 es
precision mediump float;
in vec4 color;
out vec4 result;
void main()
{
    result = color;
})";

// Tests that changing an active program invalidates the sampler metadata properly.
TEST_P(StateChangeTestES3, SamplerMetadataUpdateOnSetProgram)
{
    GLVertexArray vertexArray;
    glBindVertexArray(vertexArray);

    // Create a simple framebuffer.
    GLTexture texture1, texture2;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Create 2 shader programs differing only in the number of active samplers.
    ANGLE_GL_PROGRAM(program1, kSamplerMetadataVertexShader0, kSamplerMetadataFragmentShader);
    glUseProgram(program1);
    glUniform1i(glGetUniformLocation(program1, "texture"), 0);
    ANGLE_GL_PROGRAM(program2, kSamplerMetadataVertexShader1, kSamplerMetadataFragmentShader);
    glUseProgram(program2);
    glUniform1i(glGetUniformLocation(program2, "texture1"), 0);
    glUniform1i(glGetUniformLocation(program2, "texture2"), 0);

    // Draw a solid green color to the framebuffer.
    glUseProgram(program1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Test that our first program is good.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Bind a different program that uses more samplers.
    // Draw another quad that depends on the sampler metadata.
    glUseProgram(program2);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Flush via ReadPixels and check that it's still green.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    ASSERT_GL_NO_ERROR();
}

// Tests that redefining Buffer storage syncs with the Transform Feedback object.
TEST_P(StateChangeTestES3, RedefineTransformFeedbackBuffer)
{
    // Create the most simple program possible - simple a passthrough for a float attribute.
    constexpr char kVertexShader[] = R"(#version 300 es
in float valueIn;
out float valueOut;
void main()
{
    gl_Position = vec4(0, 0, 0, 0);
    valueOut = valueIn;
})";

    constexpr char kFragmentShader[] = R"(#version 300 es
out mediump float dummy;
void main()
{
    dummy = 1.0;
})";

    std::vector<std::string> tfVaryings = {"valueOut"};
    ANGLE_GL_PROGRAM_TRANSFORM_FEEDBACK(program, kVertexShader, kFragmentShader, tfVaryings,
                                        GL_SEPARATE_ATTRIBS);
    glUseProgram(program);

    GLint attribLoc = glGetAttribLocation(program, "valueIn");
    ASSERT_NE(-1, attribLoc);

    // Disable rasterization - we're not interested in the framebuffer.
    glEnable(GL_RASTERIZER_DISCARD);

    // Initialize a float vertex buffer with 1.0.
    std::vector<GLfloat> data1(16, 1.0);
    GLsizei size1 = static_cast<GLsizei>(sizeof(GLfloat) * data1.size());

    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, size1, data1.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(attribLoc, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(attribLoc);

    ASSERT_GL_NO_ERROR();

    // Initialize a same-sized XFB buffer.
    GLBuffer xfbBuffer;
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, xfbBuffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, size1, nullptr, GL_STATIC_DRAW);

    // Draw with XFB enabled.
    GLTransformFeedback xfb;
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, xfb);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, xfbBuffer);

    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, 16);
    glEndTransformFeedback();

    ASSERT_GL_NO_ERROR();

    // Verify the XFB stage caught the 1.0 attribute values.
    void *mapped1     = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, size1, GL_MAP_READ_BIT);
    GLfloat *asFloat1 = reinterpret_cast<GLfloat *>(mapped1);
    std::vector<GLfloat> actualData1(asFloat1, asFloat1 + data1.size());
    EXPECT_EQ(data1, actualData1);
    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);

    // Now, reinitialize the XFB buffer to a larger size, and draw with 2.0.
    std::vector<GLfloat> data2(128, 2.0);
    const GLsizei size2 = static_cast<GLsizei>(sizeof(GLfloat) * data2.size());
    glBufferData(GL_ARRAY_BUFFER, size2, data2.data(), GL_STATIC_DRAW);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, size2, nullptr, GL_STATIC_DRAW);

    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, 128);
    glEndTransformFeedback();

    ASSERT_GL_NO_ERROR();

    // Verify the XFB stage caught the 2.0 attribute values.
    void *mapped2     = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, size2, GL_MAP_READ_BIT);
    GLfloat *asFloat2 = reinterpret_cast<GLfloat *>(mapped2);
    std::vector<GLfloat> actualData2(asFloat2, asFloat2 + data2.size());
    EXPECT_EQ(data2, actualData2);
    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
}

// Simple state change tests for line loop drawing. There is some very specific handling of line
// line loops in Vulkan and we need to test switching between drawElements and drawArrays calls to
// validate every edge cases.
class LineLoopStateChangeTest : public StateChangeTest
{
  protected:
    LineLoopStateChangeTest()
    {
        setWindowWidth(32);
        setWindowHeight(32);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void validateSquareAndHourglass()
    {
        ASSERT_GL_NO_ERROR();

        int quarterWidth  = getWindowWidth() / 4;
        int quarterHeight = getWindowHeight() / 4;

        // Bottom left
        EXPECT_PIXEL_COLOR_EQ(quarterWidth, quarterHeight, GLColor::blue);

        // Top left
        EXPECT_PIXEL_COLOR_EQ(quarterWidth, (quarterHeight * 3), GLColor::blue);

        // Top right
        // The last pixel isn't filled on a line loop so we check the pixel right before.
        EXPECT_PIXEL_COLOR_EQ((quarterWidth * 3), (quarterHeight * 3) - 1, GLColor::blue);

        // dead center to validate the hourglass.
        EXPECT_PIXEL_COLOR_EQ((quarterWidth * 2), quarterHeight * 2, GLColor::blue);

        // Verify line is closed between the 2 last vertices
        EXPECT_PIXEL_COLOR_EQ((quarterWidth * 2), quarterHeight, GLColor::blue);
    }

    GLint mPositionLocation;
};

// Draw an hourglass with a drawElements call followed by a square with drawArrays.
TEST_P(LineLoopStateChangeTest, DrawElementsThenDrawArrays)
{
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());
    glUseProgram(program);

    // We expect to draw a square with these 4 vertices with a drawArray call.
    std::vector<Vector3> vertices;
    CreatePixelCenterWindowCoords({{8, 8}, {8, 24}, {24, 8}, {24, 24}}, getWindowWidth(),
                                  getWindowHeight(), &vertices);

    // If we use these indices to draw however, we should be drawing an hourglass.
    auto indices = std::vector<GLushort>{0, 2, 1, 3};

    mPositionLocation = glGetAttribLocation(program, essl1_shaders::PositionAttrib());
    ASSERT_NE(-1, mPositionLocation);

    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * vertices.size(), vertices.data(),
                 GL_STATIC_DRAW);

    GLBuffer indexBuffer;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort), &indices[0],
                 GL_STATIC_DRAW);

    glVertexAttribPointer(mPositionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(mPositionLocation);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, nullptr);  // hourglass
    glDrawArrays(GL_LINE_LOOP, 0, 4);                             // square
    glDisableVertexAttribArray(mPositionLocation);

    validateSquareAndHourglass();
}

// Draw line loop using a drawArrays followed by an hourglass with drawElements.
TEST_P(LineLoopStateChangeTest, DrawArraysThenDrawElements)
{
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());
    glUseProgram(program);

    // We expect to draw a square with these 4 vertices with a drawArray call.
    std::vector<Vector3> vertices;
    CreatePixelCenterWindowCoords({{8, 8}, {8, 24}, {24, 8}, {24, 24}}, getWindowWidth(),
                                  getWindowHeight(), &vertices);

    // If we use these indices to draw however, we should be drawing an hourglass.
    auto indices = std::vector<GLushort>{0, 2, 1, 3};

    mPositionLocation = glGetAttribLocation(program, essl1_shaders::PositionAttrib());
    ASSERT_NE(-1, mPositionLocation);

    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * vertices.size(), vertices.data(),
                 GL_STATIC_DRAW);

    GLBuffer indexBuffer;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort), &indices[0],
                 GL_STATIC_DRAW);

    glVertexAttribPointer(mPositionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(mPositionLocation);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_LINE_LOOP, 0, 4);                             // square
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, nullptr);  // hourglass
    glDisableVertexAttribArray(mPositionLocation);

    validateSquareAndHourglass();
}

// Simple state change tests, primarily focused on basic object lifetime and dependency management
// with back-ends that don't support that automatically (i.e. Vulkan).
class SimpleStateChangeTest : public ANGLETest
{
  protected:
    static constexpr int kWindowSize = 64;

    SimpleStateChangeTest()
    {
        setWindowWidth(kWindowSize);
        setWindowHeight(kWindowSize);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void simpleDrawWithBuffer(GLBuffer *buffer);
    void simpleDrawWithColor(const GLColor &color);
};

class SimpleStateChangeTestES3 : public SimpleStateChangeTest
{
};

constexpr char kSimpleVertexShader[] = R"(attribute vec2 position;
attribute vec4 color;
varying vec4 vColor;
void main()
{
    gl_Position = vec4(position, 0, 1);
    vColor = color;
}
)";

constexpr char kSimpleFragmentShader[] = R"(precision mediump float;
varying vec4 vColor;
void main()
{
    gl_FragColor = vColor;
}
)";

void SimpleStateChangeTest::simpleDrawWithBuffer(GLBuffer *buffer)
{
    ANGLE_GL_PROGRAM(program, kSimpleVertexShader, kSimpleFragmentShader);
    glUseProgram(program);

    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);

    glBindBuffer(GL_ARRAY_BUFFER, *buffer);
    glVertexAttribPointer(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glEnableVertexAttribArray(colorLoc);

    drawQuad(program, "position", 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
}

void SimpleStateChangeTest::simpleDrawWithColor(const GLColor &color)
{
    std::vector<GLColor> colors(6, color);
    GLBuffer colorBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(GLColor), colors.data(), GL_STATIC_DRAW);
    simpleDrawWithBuffer(&colorBuffer);
}

// Test that we can do a drawElements call successfully after making a drawArrays call in the same
// frame.
TEST_P(SimpleStateChangeTest, DrawArraysThenDrawElements)
{
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());
    glUseProgram(program);

    // We expect to draw a triangle with the first 3 points to the left, then another triangle with
    // the last 3 vertices using a drawElements call.
    auto vertices = std::vector<Vector3>{{-1.0f, -1.0f, 0.0f},
                                         {-1.0f, 1.0f, 0.0f},
                                         {0.0f, 0.0f, 0.0f},
                                         {1.0f, 1.0f, 0.0f},
                                         {1.0f, -1.0f, 0.0f}};

    // If we use these indices to draw we'll be using the last 2 vertex only to draw.
    auto indices = std::vector<GLushort>{2, 3, 4};

    GLint positionLocation = glGetAttribLocation(program, essl1_shaders::PositionAttrib());
    ASSERT_NE(-1, positionLocation);

    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * vertices.size(), vertices.data(),
                 GL_STATIC_DRAW);

    GLBuffer indexBuffer;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort), &indices[0],
                 GL_STATIC_DRAW);

    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLocation);

    for (int i = 0; i < 10; i++)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 3);                             // triangle to the left
        glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr);  // triangle to the right
        swapBuffers();
    }
    glDisableVertexAttribArray(positionLocation);

    ASSERT_GL_NO_ERROR();

    int quarterWidth = getWindowWidth() / 4;
    int halfHeight   = getWindowHeight() / 2;

    // Validate triangle to the left
    EXPECT_PIXEL_COLOR_EQ(quarterWidth, halfHeight, GLColor::blue);

    // Validate triangle to the right
    EXPECT_PIXEL_COLOR_EQ((quarterWidth * 3), halfHeight, GLColor::blue);
}

// Handles deleting a Buffer when it's being used.
TEST_P(SimpleStateChangeTest, DeleteBufferInUse)
{
    std::vector<GLColor> colorData(6, GLColor::red);

    GLBuffer buffer;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLColor) * colorData.size(), colorData.data(),
                 GL_STATIC_DRAW);

    simpleDrawWithBuffer(&buffer);

    buffer.reset();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Tests that resizing a Buffer during a draw works as expected.
TEST_P(SimpleStateChangeTest, RedefineBufferInUse)
{
    std::vector<GLColor> redColorData(6, GLColor::red);

    GLBuffer buffer;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLColor) * redColorData.size(), redColorData.data(),
                 GL_STATIC_DRAW);

    // Trigger a pull from the buffer.
    simpleDrawWithBuffer(&buffer);

    // Redefine the buffer that's in-flight.
    std::vector<GLColor> greenColorData(1024, GLColor::green);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLColor) * greenColorData.size(), greenColorData.data(),
                 GL_STATIC_DRAW);

    // Trigger the flush and verify the first draw worked.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Draw again and verify the new data is correct.
    simpleDrawWithBuffer(&buffer);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests updating a buffer's contents while in use, without redefining it.
TEST_P(SimpleStateChangeTest, UpdateBufferInUse)
{
    std::vector<GLColor> redColorData(6, GLColor::red);

    GLBuffer buffer;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLColor) * redColorData.size(), redColorData.data(),
                 GL_STATIC_DRAW);

    // Trigger a pull from the buffer.
    simpleDrawWithBuffer(&buffer);

    // Update the buffer that's in-flight.
    std::vector<GLColor> greenColorData(6, GLColor::green);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLColor) * greenColorData.size(),
                    greenColorData.data());

    // Trigger the flush and verify the first draw worked.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Draw again and verify the new data is correct.
    simpleDrawWithBuffer(&buffer);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests that deleting an in-flight Texture does not immediately delete the resource.
TEST_P(SimpleStateChangeTest, DeleteTextureInUse)
{
    std::array<GLColor, 4> colors = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    draw2DTexturedQuad(0.5f, 1.0f, true);
    tex.reset();
    EXPECT_GL_NO_ERROR();

    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);
}

// Tests that modifying a texture parameter in-flight does not cause problems.
TEST_P(SimpleStateChangeTest, ChangeTextureFilterModeBetweenTwoDraws)
{
    std::array<GLColor, 4> colors = {
        {GLColor::black, GLColor::white, GLColor::black, GLColor::white}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw to the left side of the window only with NEAREST.
    glViewport(0, 0, getWindowWidth() / 2, getWindowHeight());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    draw2DTexturedQuad(0.5f, 1.0f, true);

    // Draw to the right side of the window only with LINEAR.
    glViewport(getWindowWidth() / 2, 0, getWindowWidth() / 2, getWindowHeight());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    draw2DTexturedQuad(0.5f, 1.0f, true);
    EXPECT_GL_NO_ERROR();

    glViewport(0, 0, getWindowWidth(), getWindowHeight());

    // The first half (left) should be only black followed by plain white.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::black);
    EXPECT_PIXEL_COLOR_EQ(1, 0, GLColor::black);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 3, 0, GLColor::white);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 4, 0, GLColor::white);

    // The second half (right) should be a gradient so we shouldn't find plain black/white in the
    // middle.
    EXPECT_NE(angle::ReadColor((getWindowWidth() / 4) * 3, 0), GLColor::black);
    EXPECT_NE(angle::ReadColor((getWindowWidth() / 4) * 3, 0), GLColor::white);
}

// Tests that bind the same texture all the time between different draw calls.
TEST_P(SimpleStateChangeTest, RebindTextureDrawAgain)
{
    GLuint program = get2DTexturedQuadProgram();
    glUseProgram(program);

    std::array<GLColor, 4> colors = {{GLColor::cyan, GLColor::cyan, GLColor::cyan, GLColor::cyan}};

    // Setup the texture
    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Setup the vertex array to draw a quad.
    GLint positionLocation = glGetAttribLocation(program, "position");
    setupQuadVertexBuffer(1.0f, 1.0f);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(positionLocation);

    // Draw quad
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Bind again
    glBindTexture(GL_TEXTURE_2D, tex);
    ASSERT_GL_NO_ERROR();

    // Draw again, should still work.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Validate whole surface is filled with cyan.
    int h = getWindowHeight() - 1;
    int w = getWindowWidth() - 1;

    EXPECT_PIXEL_RECT_EQ(0, 0, w, h, GLColor::cyan);
}

// Tests that we can draw with a texture, modify the texture with a texSubImage, and then draw again
// correctly.
TEST_P(SimpleStateChangeTest, DrawWithTextureTexSubImageThenDrawAgain)
{
    GLuint program = get2DTexturedQuadProgram();
    ASSERT_NE(0u, program);
    glUseProgram(program);

    std::array<GLColor, 4> colors    = {{GLColor::red, GLColor::red, GLColor::red, GLColor::red}};
    std::array<GLColor, 4> subColors = {
        {GLColor::green, GLColor::green, GLColor::green, GLColor::green}};

    // Setup the texture
    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Setup the vertex array to draw a quad.
    GLint positionLocation = glGetAttribLocation(program, "position");
    setupQuadVertexBuffer(1.0f, 1.0f);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(positionLocation);

    // Draw quad
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Update bottom-half of texture with green.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, subColors.data());
    ASSERT_GL_NO_ERROR();

    // Draw again, should still work.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Validate first half of the screen is red and the bottom is green.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, getWindowHeight() / 4 * 3, GLColor::red);
}

// Test that we can alternate between textures between different draws.
TEST_P(SimpleStateChangeTest, DrawTextureAThenTextureBThenTextureA)
{
    GLuint program = get2DTexturedQuadProgram();
    glUseProgram(program);

    std::array<GLColor, 4> colorsTex1 = {
        {GLColor::cyan, GLColor::cyan, GLColor::cyan, GLColor::cyan}};

    std::array<GLColor, 4> colorsTex2 = {
        {GLColor::magenta, GLColor::magenta, GLColor::magenta, GLColor::magenta}};

    // Setup the texture
    GLTexture tex1;
    glBindTexture(GL_TEXTURE_2D, tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colorsTex1.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLTexture tex2;
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colorsTex2.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Setup the vertex array to draw a quad.
    GLint positionLocation = glGetAttribLocation(program, "position");
    setupQuadVertexBuffer(1.0f, 1.0f);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(positionLocation);

    // Draw quad
    glBindTexture(GL_TEXTURE_2D, tex1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Bind again, draw again
    glBindTexture(GL_TEXTURE_2D, tex2);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Bind again, draw again
    glBindTexture(GL_TEXTURE_2D, tex1);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Validate whole surface is filled with cyan.
    int h = getWindowHeight() - 1;
    int w = getWindowWidth() - 1;

    EXPECT_PIXEL_RECT_EQ(0, 0, w, h, GLColor::cyan);
}

// Tests that redefining an in-flight Texture does not affect the in-flight resource.
TEST_P(SimpleStateChangeTest, RedefineTextureInUse)
{
    std::array<GLColor, 4> colors = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Draw with the first texture.
    draw2DTexturedQuad(0.5f, 1.0f, true);

    // Redefine the in-flight texture.
    constexpr int kBigSize = 32;
    std::vector<GLColor> bigColors;
    for (int y = 0; y < kBigSize; ++y)
    {
        for (int x = 0; x < kBigSize; ++x)
        {
            bool xComp = x < kBigSize / 2;
            bool yComp = y < kBigSize / 2;
            if (yComp)
            {
                bigColors.push_back(xComp ? GLColor::cyan : GLColor::magenta);
            }
            else
            {
                bigColors.push_back(xComp ? GLColor::yellow : GLColor::white);
            }
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, bigColors.data());
    EXPECT_GL_NO_ERROR();

    // Verify the first draw had the correct data via ReadPixels.
    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);

    // Draw and verify with the redefined data.
    draw2DTexturedQuad(0.5f, 1.0f, true);
    EXPECT_GL_NO_ERROR();

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::cyan);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::magenta);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::yellow);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::white);
}

// Test updating a Texture's contents while in use by GL works as expected.
TEST_P(SimpleStateChangeTest, UpdateTextureInUse)
{
    std::array<GLColor, 4> rgby = {{GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgby.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Draw RGBY to the Framebuffer. The texture is now in-use by GL.
    draw2DTexturedQuad(0.5f, 1.0f, true);

    // Update the texture to be YBGR, while the Texture is in-use. Should not affect the draw.
    std::array<GLColor, 4> ybgr = {{GLColor::yellow, GLColor::blue, GLColor::green, GLColor::red}};
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, ybgr.data());
    ASSERT_GL_NO_ERROR();

    // Check the Framebuffer. The draw call should have completed with the original RGBY data.
    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);

    // Draw again to the Framebuffer. The second draw call should use the updated YBGR data.
    draw2DTexturedQuad(0.5f, 1.0f, true);

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::yellow);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::red);
    ASSERT_GL_NO_ERROR();
}

// Tests deleting a Framebuffer that is in use.
TEST_P(SimpleStateChangeTest, DeleteFramebufferInUse)
{
    constexpr int kSize = 16;

    // Create a simple framebuffer.
    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glViewport(0, 0, kSize, kSize);

    // Draw a solid red color to the framebuffer.
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f, 1.0f, true);

    // Delete the framebuffer while the call is in flight.
    framebuffer.reset();

    // Make a new framebuffer so we can read back the texture.
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Flush via ReadPixels and check red was drawn.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    ASSERT_GL_NO_ERROR();
}

// This test was made to reproduce a specific issue with our Vulkan backend where were releasing
// buffers too early. The test has 2 textures, we first create a texture and update it with
// multiple updates, but we don't use it right away, we instead draw using another texture
// then we bind the first texture and draw with it.
TEST_P(SimpleStateChangeTest, DynamicAllocationOfMemoryForTextures)
{
    constexpr int kSize = 64;

    GLuint program = get2DTexturedQuadProgram();
    glUseProgram(program);

    std::vector<GLColor> greenPixels(kSize * kSize, GLColor::green);
    std::vector<GLColor> redPixels(kSize * kSize, GLColor::red);
    GLTexture texture1;
    glBindTexture(GL_TEXTURE_2D, texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < 100; i++)
    {
        // We do this a lot of time to make sure we use multiple buffers in the vulkan backend.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE,
                        greenPixels.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    ASSERT_GL_NO_ERROR();

    GLTexture texture2;
    glBindTexture(GL_TEXTURE_2D, texture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, redPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Setup the vertex array to draw a quad.
    GLint positionLocation = glGetAttribLocation(program, "position");
    setupQuadVertexBuffer(1.0f, 1.0f);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(positionLocation);

    // Draw quad with texture 2 while texture 1 has "staged" changes that have not been flushed yet.
    glBindTexture(GL_TEXTURE_2D, texture2);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // If we now try to draw with texture1, we should trigger the issue.
    glBindTexture(GL_TEXTURE_2D, texture1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests deleting a Framebuffer that is in use.
TEST_P(SimpleStateChangeTest, RedefineFramebufferInUse)
{
    constexpr int kSize = 16;

    // Create a simple framebuffer.
    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glViewport(0, 0, kSize, kSize);

    // Draw red to the framebuffer.
    simpleDrawWithColor(GLColor::red);

    // Change the framebuffer while the call is in flight to a new texture.
    GLTexture otherTexture;
    glBindTexture(GL_TEXTURE_2D, otherTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, otherTexture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Draw green to the framebuffer. Verify the color.
    simpleDrawWithColor(GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Make a new framebuffer so we can read back the first texture and verify red.
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    ASSERT_GL_NO_ERROR();
}

// Tests that redefining a Framebuffer Texture Attachment works as expected.
TEST_P(SimpleStateChangeTest, RedefineFramebufferTexture)
{
    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // Bind a simple 8x8 texture to the framebuffer, draw red.
    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    glViewport(0, 0, 8, 8);
    simpleDrawWithColor(GLColor::red);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red) << "first draw should be red";

    // Redefine the texture to 32x32, draw green. Verify we get what we expect.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glViewport(0, 0, 32, 32);
    simpleDrawWithColor(GLColor::green);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green) << "second draw should be green";
}

// Validates disabling cull face really disables it.
TEST_P(SimpleStateChangeTest, EnableAndDisableCullFace)
{
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());
    glUseProgram(program);

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_CULL_FACE);

    glCullFace(GL_FRONT);

    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);

    ASSERT_GL_NO_ERROR();

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::transparentBlack);

    // Disable cull face and redraw, then make sure we have the quad drawn.
    glDisable(GL_CULL_FACE);

    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);

    ASSERT_GL_NO_ERROR();

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

TEST_P(SimpleStateChangeTest, ScissorTest)
{
    // This test validates this order of state changes:
    // 1- Set scissor but don't enable it, validate its not used.
    // 2- Enable it and validate its working.
    // 3- Disable the scissor validate its not used anymore.

    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());

    glClear(GL_COLOR_BUFFER_BIT);

    // Set the scissor region, but don't enable it yet.
    glScissor(getWindowWidth() / 4, getWindowHeight() / 4, getWindowWidth() / 2,
              getWindowHeight() / 2);

    // Fill the whole screen with a quad.
    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);

    ASSERT_GL_NO_ERROR();

    // Test outside, scissor isnt enabled so its red.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Test inside, red of the fragment shader.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 2, getWindowHeight() / 2, GLColor::red);

    // Clear everything and start over with the test enabled.
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);

    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);

    ASSERT_GL_NO_ERROR();

    // Test outside the scissor test, pitch black.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::transparentBlack);

    // Test inside, red of the fragment shader.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 2, getWindowHeight() / 2, GLColor::red);

    // Now disable the scissor test, do it again, and verify the region isn't used
    // for the scissor test.
    glDisable(GL_SCISSOR_TEST);

    // Clear everything and start over with the test enabled.
    glClear(GL_COLOR_BUFFER_BIT);

    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);

    ASSERT_GL_NO_ERROR();

    // Test outside, scissor isnt enabled so its red.
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Test inside, red of the fragment shader.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 2, getWindowHeight() / 2, GLColor::red);
}

// This test validates we are able to change the valid of a uniform dynamically.
TEST_P(SimpleStateChangeTest, UniformUpdateTest)
{
    constexpr char kPositionUniformVertexShader[] = R"(
precision mediump float;
attribute vec2 position;
uniform vec2 uniPosModifier;
void main()
{
    gl_Position = vec4(position + uniPosModifier, 0, 1);
})";

    ANGLE_GL_PROGRAM(program, kPositionUniformVertexShader, essl1_shaders::fs::UniformColor());
    glUseProgram(program);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLint posUniformLocation = glGetUniformLocation(program, "uniPosModifier");
    ASSERT_NE(posUniformLocation, -1);
    GLint colorUniformLocation = glGetUniformLocation(program, essl1_shaders::ColorUniform());
    ASSERT_NE(colorUniformLocation, -1);

    // draw a red quad to the left side.
    glUniform2f(posUniformLocation, -0.5, 0.0);
    glUniform4f(colorUniformLocation, 1.0, 0.0, 0.0, 1.0);
    drawQuad(program.get(), "position", 0.0f, 0.5f, true);

    // draw a green quad to the right side.
    glUniform2f(posUniformLocation, 0.5, 0.0);
    glUniform4f(colorUniformLocation, 0.0, 1.0, 0.0, 1.0);
    drawQuad(program.get(), "position", 0.0f, 0.5f, true);

    ASSERT_GL_NO_ERROR();

    // Test the center of the left quad. Should be red.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 4, getWindowHeight() / 2, GLColor::red);

    // Test the center of the right quad. Should be green.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 4 * 3, getWindowHeight() / 2, GLColor::green);
}

// Tests that changing the storage of a Renderbuffer currently in use by GL works as expected.
TEST_P(SimpleStateChangeTest, RedefineRenderbufferInUse)
{
    GLRenderbuffer renderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);

    ASSERT_GL_NO_ERROR();
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ANGLE_GL_PROGRAM(program, kSimpleVertexShader, kSimpleFragmentShader);
    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);

    // Set up and draw red to the left half the screen.
    std::vector<GLColor> redData(6, GLColor::red);
    GLBuffer vertexBufferRed;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferRed);
    glBufferData(GL_ARRAY_BUFFER, redData.size() * sizeof(GLColor), redData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glEnableVertexAttribArray(colorLoc);

    glViewport(0, 0, 16, 16);
    drawQuad(program, "position", 0.5f, 1.0f, true);

    // Immediately redefine the Renderbuffer.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);

    // Set up and draw green to the right half of the screen.
    std::vector<GLColor> greenData(6, GLColor::green);
    GLBuffer vertexBufferGreen;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufferGreen);
    glBufferData(GL_ARRAY_BUFFER, greenData.size() * sizeof(GLColor), greenData.data(),
                 GL_STATIC_DRAW);
    glVertexAttribPointer(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glEnableVertexAttribArray(colorLoc);

    glViewport(0, 0, 64, 64);
    drawQuad(program, "position", 0.5f, 1.0f, true);

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Validate that we can draw -> change frame buffer size -> draw and we'll be rendering
// at the full size of the new framebuffer.
TEST_P(SimpleStateChangeTest, ChangeFramebufferSizeBetweenTwoDraws)
{
    constexpr size_t kSmallTextureSize = 2;
    constexpr size_t kBigTextureSize   = 4;

    // Create 2 textures, one of 2x2 and the other 4x4
    GLTexture texture1;
    glBindTexture(GL_TEXTURE_2D, texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSmallTextureSize, kSmallTextureSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    ASSERT_GL_NO_ERROR();

    GLTexture texture2;
    glBindTexture(GL_TEXTURE_2D, texture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kBigTextureSize, kBigTextureSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    ASSERT_GL_NO_ERROR();

    // A framebuffer for each texture to draw on.
    GLFramebuffer framebuffer1;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture1, 0);
    ASSERT_GL_NO_ERROR();
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    GLFramebuffer framebuffer2;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture2, 0);
    ASSERT_GL_NO_ERROR();
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::UniformColor());
    glUseProgram(program);
    GLint uniformLocation = glGetUniformLocation(program, essl1_shaders::ColorUniform());
    ASSERT_NE(uniformLocation, -1);

    // Bind to the first framebuffer for drawing.
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer1);

    // Set a scissor, that will trigger setting the internal scissor state in Vulkan to
    // (0,0,framebuffer.width, framebuffer.height) size since the scissor isn't enabled.
    glScissor(0, 0, 16, 16);
    ASSERT_GL_NO_ERROR();

    // Set color to red.
    glUniform4f(uniformLocation, 1.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, kSmallTextureSize, kSmallTextureSize);

    // Draw a full sized red quad
    drawQuad(program, essl1_shaders::PositionAttrib(), 1.0f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    // Bind to the second (bigger) framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer2);
    glViewport(0, 0, kBigTextureSize, kBigTextureSize);

    ASSERT_GL_NO_ERROR();

    // Set color to green.
    glUniform4f(uniformLocation, 0.0f, 1.0f, 0.0f, 1.0f);

    // Draw again and we should fill everything with green and expect everything to be green.
    drawQuad(program, essl1_shaders::PositionAttrib(), 1.0f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    EXPECT_PIXEL_RECT_EQ(0, 0, kBigTextureSize, kBigTextureSize, GLColor::green);
}

// Tries to relink a program in use and use it again to draw something else.
TEST_P(SimpleStateChangeTest, RelinkProgram)
{
    const GLuint program = glCreateProgram();

    GLuint vs     = CompileShader(GL_VERTEX_SHADER, essl1_shaders::vs::Simple());
    GLuint blueFs = CompileShader(GL_FRAGMENT_SHADER, essl1_shaders::fs::Blue());
    GLuint redFs  = CompileShader(GL_FRAGMENT_SHADER, essl1_shaders::fs::Red());

    glAttachShader(program, vs);
    glAttachShader(program, blueFs);

    glLinkProgram(program);
    CheckLinkStatusAndReturnProgram(program, true);

    glClear(GL_COLOR_BUFFER_BIT);
    std::vector<Vector3> vertices = {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f},
                                     {-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0, 1.0f, 0.0f}};
    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * vertices.size(), vertices.data(),
                 GL_STATIC_DRAW);
    const GLint positionLocation = glGetAttribLocation(program, essl1_shaders::PositionAttrib());
    ASSERT_NE(-1, positionLocation);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLocation);

    // Draw a blue triangle to the right
    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Relink to draw red to the left
    glDetachShader(program, blueFs);
    glAttachShader(program, redFs);

    glLinkProgram(program);
    CheckLinkStatusAndReturnProgram(program, true);

    glDrawArrays(GL_TRIANGLES, 3, 3);

    ASSERT_GL_NO_ERROR();

    glDisableVertexAttribArray(positionLocation);

    // Verify we drew red and green in the right places.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 2, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(0, getWindowHeight() / 2, GLColor::red);

    glDeleteShader(vs);
    glDeleteShader(blueFs);
    glDeleteShader(redFs);
    glDeleteProgram(program);
}

// Creates a program that uses uniforms and then immediately release it and then use it. Should be
// valid.
TEST_P(SimpleStateChangeTest, ReleaseShaderInUseThatReadsFromUniforms)
{
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::UniformColor());
    glUseProgram(program);

    const GLint uniformLoc = glGetUniformLocation(program, essl1_shaders::ColorUniform());
    EXPECT_NE(-1, uniformLoc);

    // Set color to red.
    glUniform4f(uniformLoc, 1.0f, 0.0f, 0.0f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT);
    std::vector<Vector3> vertices = {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}};
    GLBuffer vertexBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices[0]) * vertices.size(), vertices.data(),
                 GL_STATIC_DRAW);
    const GLint positionLocation = glGetAttribLocation(program, essl1_shaders::PositionAttrib());
    ASSERT_NE(-1, positionLocation);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLocation);

    // Release program while its in use.
    glDeleteProgram(program);

    // Draw a red triangle
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Set color to green
    glUniform4f(uniformLoc, 1.0f, 0.0f, 0.0f, 1.0f);

    // Draw a green triangle
    glDrawArrays(GL_TRIANGLES, 0, 3);

    ASSERT_GL_NO_ERROR();

    glDisableVertexAttribArray(positionLocation);

    glUseProgram(0);

    // Verify we drew red in the end since thats the last draw.
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() / 2, 0, GLColor::red);
}

// Tests that sampler sync isn't masked by program textures.
TEST_P(SimpleStateChangeTestES3, SamplerSyncNotTiedToProgram)
{
    // Create a sampler with NEAREST filtering.
    GLSampler sampler;
    glBindSampler(0, sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    // Draw with a program that uses no textures.
    ANGLE_GL_PROGRAM(program1, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());
    drawQuad(program1, essl1_shaders::PositionAttrib(), 0.5f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);

    // Create a simple texture with four colors and linear filtering.
    constexpr GLsizei kSize       = 2;
    std::array<GLColor, 4> pixels = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};
    GLTexture redTex;
    glBindTexture(GL_TEXTURE_2D, redTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Create a program that uses the texture.
    constexpr char kVS[] = R"(attribute vec4 position;
varying vec2 texCoord;
void main()
{
    gl_Position = position;
    texCoord = position.xy * 0.5 + vec2(0.5);
})";

    constexpr char kFS[] = R"(precision mediump float;
varying vec2 texCoord;
uniform sampler2D tex;
void main()
{
    gl_FragColor = texture2D(tex, texCoord);
})";

    // Draw. The sampler should override the clamp wrap mode with nearest.
    ANGLE_GL_PROGRAM(program2, kVS, kFS);
    ASSERT_EQ(0, glGetUniformLocation(program2, "tex"));
    drawQuad(program2, "position", 0.5f);
    ASSERT_GL_NO_ERROR();

    constexpr int kHalfSize = kWindowSize / 2;

    EXPECT_PIXEL_RECT_EQ(0, 0, kHalfSize, kHalfSize, GLColor::red);
    EXPECT_PIXEL_RECT_EQ(kHalfSize, 0, kHalfSize, kHalfSize, GLColor::green);
    EXPECT_PIXEL_RECT_EQ(0, kHalfSize, kHalfSize, kHalfSize, GLColor::blue);
    EXPECT_PIXEL_RECT_EQ(kHalfSize, kHalfSize, kHalfSize, kHalfSize, GLColor::yellow);
}

static constexpr char kColorVS[] = R"(attribute vec2 position;
attribute vec4 color;
varying vec4 vColor;
void main()
{
    gl_Position = vec4(position, 0, 1);
    vColor = color;
})";

static constexpr char kColorFS[] = R"(precision mediump float;
varying vec4 vColor;
void main()
{
    gl_FragColor = vColor;
})";

class ValidationStateChangeTest : public ANGLETest
{
  protected:
    ValidationStateChangeTest()
    {
        setWindowWidth(64);
        setWindowHeight(64);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }
};

class WebGL2ValidationStateChangeTest : public ValidationStateChangeTest
{
  protected:
    WebGL2ValidationStateChangeTest() { setWebGLCompatibilityEnabled(true); }
};

class ValidationStateChangeTestES31 : public ANGLETest
{
};

// Tests that mapping and unmapping an array buffer in various ways causes rendering to fail.
// This isn't guaranteed to produce an error by GL. But we assume ANGLE always errors.
TEST_P(ValidationStateChangeTest, MapBufferAndDraw)
{
    // Initialize program and set up state.
    ANGLE_GL_PROGRAM(program, kColorVS, kColorFS);

    glUseProgram(program);
    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLoc);
    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);

    const std::array<Vector3, 6> &quadVertices = GetQuadVertices();
    const size_t posBufferSize                 = quadVertices.size() * sizeof(Vector3);

    GLBuffer posBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, posBuffer);
    glBufferData(GL_ARRAY_BUFFER, posBufferSize, quadVertices.data(), GL_STATIC_DRAW);

    // Start with position enabled.
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLoc);

    std::vector<GLColor> colorVertices(6, GLColor::blue);
    const size_t colorBufferSize = sizeof(GLColor) * 6;

    GLBuffer colorBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
    glBufferData(GL_ARRAY_BUFFER, colorBufferSize, colorVertices.data(), GL_STATIC_DRAW);

    // Start with color disabled.
    glVertexAttribPointer(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glDisableVertexAttribArray(colorLoc);

    ASSERT_GL_NO_ERROR();

    // Draw without a mapped buffer. Should succeed.
    glVertexAttrib4f(colorLoc, 0, 1, 0, 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Map position buffer and draw. Should fail.
    glBindBuffer(GL_ARRAY_BUFFER, posBuffer);
    glMapBufferRange(GL_ARRAY_BUFFER, 0, posBufferSize, GL_MAP_READ_BIT);
    ASSERT_GL_NO_ERROR();

    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Map position buffer and draw should fail.";
    glUnmapBuffer(GL_ARRAY_BUFFER);

    // Map then enable color buffer. Should fail.
    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer);
    glMapBufferRange(GL_ARRAY_BUFFER, 0, colorBufferSize, GL_MAP_READ_BIT);
    glEnableVertexAttribArray(colorLoc);
    ASSERT_GL_NO_ERROR();

    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Map then enable color buffer should fail.";

    // Unmap then draw. Should succeed.
    glUnmapBuffer(GL_ARRAY_BUFFER);
    ASSERT_GL_NO_ERROR();

    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
}

// Tests that changing a vertex binding with glVertexAttribDivisor updates the mapped buffer check.
TEST_P(ValidationStateChangeTestES31, MapBufferAndDrawWithDivisor)
{
    // Seems to trigger a GL error in some edge cases. http://anglebug.com/2755
    ANGLE_SKIP_TEST_IF(IsOpenGL() && IsNVIDIA());

    // Initialize program and set up state.
    ANGLE_GL_PROGRAM(program, kColorVS, kColorFS);

    glUseProgram(program);
    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLoc);
    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);

    // Create a user vertex array.
    GLVertexArray vao;
    glBindVertexArray(vao);

    const std::array<Vector3, 6> &quadVertices = GetQuadVertices();
    const size_t posBufferSize                 = quadVertices.size() * sizeof(Vector3);

    GLBuffer posBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, posBuffer);
    glBufferData(GL_ARRAY_BUFFER, posBufferSize, quadVertices.data(), GL_STATIC_DRAW);

    // Start with position enabled.
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLoc);

    std::vector<GLColor> blueVertices(6, GLColor::blue);
    const size_t blueBufferSize = sizeof(GLColor) * 6;

    GLBuffer blueBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, blueBuffer);
    glBufferData(GL_ARRAY_BUFFER, blueBufferSize, blueVertices.data(), GL_STATIC_DRAW);

    // Start with color enabled at an unused binding.
    constexpr GLint kUnusedBinding = 3;
    ASSERT_NE(colorLoc, kUnusedBinding);
    ASSERT_NE(positionLoc, kUnusedBinding);
    glVertexAttribFormat(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0);
    glVertexAttribBinding(colorLoc, kUnusedBinding);
    glBindVertexBuffer(kUnusedBinding, blueBuffer, 0, sizeof(GLColor));
    glEnableVertexAttribArray(colorLoc);

    // Make binding 'colorLoc' use a mapped buffer.
    std::vector<GLColor> greenVertices(6, GLColor::green);
    const size_t greenBufferSize = sizeof(GLColor) * 6;
    GLBuffer greenBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, greenBuffer);
    glBufferData(GL_ARRAY_BUFFER, greenBufferSize, greenVertices.data(), GL_STATIC_DRAW);
    glMapBufferRange(GL_ARRAY_BUFFER, 0, greenBufferSize, GL_MAP_READ_BIT);
    glBindVertexBuffer(colorLoc, greenBuffer, 0, sizeof(GLColor));

    ASSERT_GL_NO_ERROR();

    // Draw without a mapped buffer. Should succeed.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);

    // Change divisor with VertexAttribDivisor. Should fail.
    glVertexAttribDivisor(colorLoc, 0);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "draw with mapped buffer should fail.";

    // Unmap the buffer. Should succeed.
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests that changing a vertex binding with glVertexAttribDivisor updates the buffer size check.
TEST_P(ValidationStateChangeTestES31, DrawPastEndOfBufferWithDivisor)
{
    // Initialize program and set up state.
    ANGLE_GL_PROGRAM(program, kColorVS, kColorFS);

    glUseProgram(program);
    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLoc);
    GLint colorLoc = glGetAttribLocation(program, "color");
    ASSERT_NE(-1, colorLoc);

    // Create a user vertex array.
    GLVertexArray vao;
    glBindVertexArray(vao);

    const std::array<Vector3, 6> &quadVertices = GetQuadVertices();
    const size_t posBufferSize                 = quadVertices.size() * sizeof(Vector3);

    GLBuffer posBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, posBuffer);
    glBufferData(GL_ARRAY_BUFFER, posBufferSize, quadVertices.data(), GL_STATIC_DRAW);

    // Start with position enabled.
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLoc);

    std::vector<GLColor> blueVertices(6, GLColor::blue);
    const size_t blueBufferSize = sizeof(GLColor) * 6;

    GLBuffer blueBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, blueBuffer);
    glBufferData(GL_ARRAY_BUFFER, blueBufferSize, blueVertices.data(), GL_STATIC_DRAW);

    // Start with color enabled at an unused binding.
    constexpr GLint kUnusedBinding = 3;
    ASSERT_NE(colorLoc, kUnusedBinding);
    ASSERT_NE(positionLoc, kUnusedBinding);
    glVertexAttribFormat(colorLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0);
    glVertexAttribBinding(colorLoc, kUnusedBinding);
    glBindVertexBuffer(kUnusedBinding, blueBuffer, 0, sizeof(GLColor));
    glEnableVertexAttribArray(colorLoc);

    // Make binding 'colorLoc' use a small buffer.
    std::vector<GLColor> greenVertices(6, GLColor::green);
    const size_t greenBufferSize = sizeof(GLColor) * 3;
    GLBuffer greenBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, greenBuffer);
    glBufferData(GL_ARRAY_BUFFER, greenBufferSize, greenVertices.data(), GL_STATIC_DRAW);
    glBindVertexBuffer(colorLoc, greenBuffer, 0, sizeof(GLColor));

    ASSERT_GL_NO_ERROR();

    // Draw without a mapped buffer. Should succeed.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);

    // Change divisor with VertexAttribDivisor. Should fail.
    glVertexAttribDivisor(colorLoc, 0);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "draw with small buffer should fail.";

    // Do a small draw. Should succeed.
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests state changes with uniform block validation.
TEST_P(ValidationStateChangeTest, UniformBlockNegativeAPI)
{
    constexpr char kVS[] = R"(#version 300 es
in vec2 position;
void main()
{
    gl_Position = vec4(position, 0, 1);
})";

    constexpr char kFS[] = R"(#version 300 es
precision mediump float;
uniform uni { vec4 vec; };
out vec4 color;
void main()
{
    color = vec;
})";

    ANGLE_GL_PROGRAM(program, kVS, kFS);
    glUseProgram(program);

    GLuint blockIndex = glGetUniformBlockIndex(program, "uni");
    ASSERT_NE(GL_INVALID_INDEX, blockIndex);

    glUniformBlockBinding(program, blockIndex, 0);

    GLBuffer uniformBuffer;
    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GLColor32F), &kFloatGreen.R, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);

    const auto &quadVertices = GetQuadVertices();

    GLBuffer positionBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    glBufferData(GL_ARRAY_BUFFER, quadVertices.size() * sizeof(Vector3), quadVertices.data(),
                 GL_STATIC_DRAW);

    GLint positionLocation = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLocation);

    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLocation);
    ASSERT_GL_NO_ERROR();

    // First draw should succeed.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Change the uniform block binding. Should fail.
    glUniformBlockBinding(program, blockIndex, 1);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_ERROR(GL_INVALID_OPERATION) << "Invalid uniform block binding should fail";

    // Reset to a correct state.
    glUniformBlockBinding(program, blockIndex, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Change the buffer binding. Should fail.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_ERROR(GL_INVALID_OPERATION) << "Setting invalid uniform buffer should fail";

    // Reset to a correct state.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Resize the buffer to be too small. Should fail.
    glBufferData(GL_UNIFORM_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Invalid buffer size should fail";
}

// Tests various state change effects on draw framebuffer validation.
TEST_P(WebGL2ValidationStateChangeTest, DrawFramebufferNegativeAPI)
{
    // Set up a simple draw from a Texture to a user Framebuffer.
    GLuint program = get2DTexturedQuadProgram();
    ASSERT_NE(0u, program);
    glUseProgram(program);

    GLint posLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, posLoc);

    const auto &quadVertices = GetQuadVertices();

    GLBuffer positionBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3) * quadVertices.size(), quadVertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(posLoc);

    constexpr size_t kSize = 2;

    GLTexture colorBufferTexture;
    glBindTexture(GL_TEXTURE_2D, colorBufferTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBufferTexture,
                           0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    std::vector<GLColor> greenColor(kSize * kSize, GLColor::green);

    GLTexture greenTexture;
    glBindTexture(GL_TEXTURE_2D, greenTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 greenColor.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Second framebuffer with a feedback loop. Initially unbound.
    GLFramebuffer loopedFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, loopedFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, greenTexture, 0);
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    ASSERT_GL_NO_ERROR();

    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Create a rendering feedback loop. Should fail.
    glBindTexture(GL_TEXTURE_2D, colorBufferTexture);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);

    // Reset to a valid state.
    glBindTexture(GL_TEXTURE_2D, greenTexture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Bind a second framebuffer with a feedback loop.
    glBindFramebuffer(GL_FRAMEBUFFER, loopedFramebuffer);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);

    // Update the framebuffer texture attachment. Should succeed.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBufferTexture,
                           0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests various state change effects on draw framebuffer validation with MRT.
TEST_P(WebGL2ValidationStateChangeTest, MultiAttachmentDrawFramebufferNegativeAPI)
{
    // Set up a program that writes to two outputs: one int and one float.
    constexpr char kVS[] = R"(#version 300 es
layout(location = 0) in vec2 position;
out vec2 texCoord;
void main()
{
    gl_Position = vec4(position, 0, 1);
    texCoord = position * 0.5 + vec2(0.5);
})";

    constexpr char kFS[] = R"(#version 300 es
precision mediump float;
in vec2 texCoord;
layout(location = 0) out vec4 outFloat;
layout(location = 1) out uvec4 outInt;
void main()
{
    outFloat = vec4(0, 1, 0, 1);
    outInt = uvec4(0, 1, 0, 1);
})";

    ANGLE_GL_PROGRAM(program, kVS, kFS);
    glUseProgram(program);

    constexpr GLint kPosLoc = 0;

    const auto &quadVertices = GetQuadVertices();

    GLBuffer positionBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3) * quadVertices.size(), quadVertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(kPosLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(kPosLoc);

    constexpr size_t kSize = 2;

    GLFramebuffer floatFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, floatFramebuffer);

    GLTexture floatTextures[2];
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, floatTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                               floatTextures[i], 0);
        ASSERT_GL_NO_ERROR();
    }

    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    GLFramebuffer intFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, intFramebuffer);

    GLTexture intTextures[2];
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, intTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, kSize, kSize, 0, GL_RGBA_INTEGER,
                     GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                               intTextures[i], 0);
        ASSERT_GL_NO_ERROR();
    }

    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();

    constexpr GLenum kColor0Enabled[]     = {GL_COLOR_ATTACHMENT0, GL_NONE};
    constexpr GLenum kColor1Enabled[]     = {GL_NONE, GL_COLOR_ATTACHMENT1};
    constexpr GLenum kColor0And1Enabled[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    // Draw float. Should work.
    glBindFramebuffer(GL_FRAMEBUFFER, floatFramebuffer);
    glDrawBuffers(2, kColor0Enabled);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR() << "Draw to float texture with correct mask";
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Set an invalid component write.
    glDrawBuffers(2, kColor0And1Enabled);
    ASSERT_GL_NO_ERROR() << "Draw to float texture with invalid mask";
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);

    // Restore state.
    glDrawBuffers(2, kColor0Enabled);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR() << "Draw to float texture with correct mask";
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Bind an invalid framebuffer. Validate failure.
    glBindFramebuffer(GL_FRAMEBUFFER, intFramebuffer);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Draw to int texture with default mask";

    // Set draw mask to a valid mask. Validate success.
    glDrawBuffers(2, kColor1Enabled);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR() << "Draw to int texture with correct mask";
}

// Tests negative API state change cases with Transform Feedback bindings.
TEST_P(WebGL2ValidationStateChangeTest, TransformFeedbackNegativeAPI)
{
    ANGLE_SKIP_TEST_IF(IsAMD() && IsOSX());

    constexpr char kFS[] = R"(#version 300 es
precision mediump float;
uniform block { vec4 color; };
out vec4 colorOut;
void main()
{
    colorOut = color;
})";

    std::vector<std::string> tfVaryings = {"gl_Position"};
    ANGLE_GL_PROGRAM_TRANSFORM_FEEDBACK(program, essl3_shaders::vs::Simple(), kFS, tfVaryings,
                                        GL_INTERLEAVED_ATTRIBS);
    glUseProgram(program);

    std::vector<Vector4> positionData;
    for (const Vector3 &quadVertex : GetQuadVertices())
    {
        positionData.emplace_back(quadVertex.x(), quadVertex.y(), quadVertex.z(), 1.0f);
    }

    GLBuffer arrayBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
    glBufferData(GL_ARRAY_BUFFER, positionData.size() * sizeof(Vector4), positionData.data(),
                 GL_STATIC_DRAW);

    GLint positionLoc = glGetAttribLocation(program, essl3_shaders::PositionAttrib());
    ASSERT_NE(-1, positionLoc);

    glVertexAttribPointer(positionLoc, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLoc);

    EXPECT_GL_NO_ERROR();

    // Set up transform feedback.
    GLTransformFeedback transformFeedback;
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, transformFeedback);

    constexpr size_t kTransformFeedbackSize = 6 * sizeof(Vector4);

    GLBuffer transformFeedbackBuffer;
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, transformFeedbackBuffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, kTransformFeedbackSize * 2, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, transformFeedbackBuffer);

    // Set up uniform buffer.
    std::vector<GLColor32F> redData(1, kFloatGreen);

    GLBuffer uniformBuffer;
    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, 128, &kFloatGreen.R, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);

    ASSERT_GL_NO_ERROR();

    // Do the draw operation. Should succeed.
    glBeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEndTransformFeedback();

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    const GLvoid *mapPointer =
        glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, kTransformFeedbackSize, GL_MAP_READ_BIT);
    ASSERT_GL_NO_ERROR();
    ASSERT_NE(nullptr, mapPointer);
    const Vector4 *typedMapPointer = reinterpret_cast<const Vector4 *>(mapPointer);
    std::vector<Vector4> actualData(typedMapPointer, typedMapPointer + 6);
    EXPECT_EQ(positionData, actualData);
    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);

    // Draw once to update validation cache.
    glBeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Bind transform feedback buffer to another binding point. Should cause a conflict.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, transformFeedbackBuffer);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEndTransformFeedback();
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Simultaneous element buffer binding should fail";

    // Reset to valid state.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEndTransformFeedback();
    ASSERT_GL_NO_ERROR();

    // Simultaneous non-vertex-array binding. Should fail.
    glBeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, transformFeedbackBuffer);
    ASSERT_GL_NO_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEndTransformFeedback();
    EXPECT_GL_ERROR(GL_INVALID_OPERATION) << "Simultaneous pack buffer binding should fail";
}

// Tests a valid rendering setup with two textures. Followed by a draw with conflicting samplers.
TEST_P(ValidationStateChangeTest, TextureConflict)
{
    ANGLE_SKIP_TEST_IF(!extensionEnabled("GL_EXT_texture_storage"));

    GLint maxTextures = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextures);
    ANGLE_SKIP_TEST_IF(maxTextures < 2);

    // Set up state.
    constexpr GLint kSize = 2;

    std::vector<GLColor> greenData(4, GLColor::green);

    GLTexture textureA;
    glBindTexture(GL_TEXTURE_2D, textureA);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 greenData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glActiveTexture(GL_TEXTURE1);

    GLTexture textureB;
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureB);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, greenData.data());
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    constexpr char kVS[] = R"(attribute vec2 position;
varying mediump vec2 texCoord;
void main()
{
    gl_Position = vec4(position, 0, 1);
    texCoord = position * 0.5 + vec2(0.5);
})";

    constexpr char kFS[] = R"(varying mediump vec2 texCoord;
uniform sampler2D texA;
uniform samplerCube texB;
void main()
{
    gl_FragColor = texture2D(texA, texCoord) + textureCube(texB, vec3(1, 0, 0));
})";

    ANGLE_GL_PROGRAM(program, kVS, kFS);
    glUseProgram(program);

    const auto &quadVertices = GetQuadVertices();

    GLBuffer arrayBuffer;
    glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
    glBufferData(GL_ARRAY_BUFFER, quadVertices.size() * sizeof(Vector3), quadVertices.data(),
                 GL_STATIC_DRAW);

    GLint positionLoc = glGetAttribLocation(program, "position");
    ASSERT_NE(-1, positionLoc);

    GLint texALoc = glGetUniformLocation(program, "texA");
    ASSERT_NE(-1, texALoc);

    GLint texBLoc = glGetUniformLocation(program, "texB");
    ASSERT_NE(-1, texBLoc);

    glUniform1i(texALoc, 0);
    glUniform1i(texBLoc, 1);

    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(positionLoc);

    ASSERT_GL_NO_ERROR();

    // First draw. Should succeed.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Second draw to ensure all state changes are flushed.
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ASSERT_GL_NO_ERROR();

    // Make the uniform use an invalid texture binding.
    glUniform1i(texBLoc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}
}  // anonymous namespace

ANGLE_INSTANTIATE_TEST(StateChangeTest, ES2_D3D9(), ES2_D3D11(), ES2_OPENGL(), ES2_VULKAN());
ANGLE_INSTANTIATE_TEST(LineLoopStateChangeTest,
                       ES2_D3D9(),
                       ES2_D3D11(),
                       ES2_OPENGL(),
                       ES2_VULKAN());
ANGLE_INSTANTIATE_TEST(StateChangeRenderTest,
                       ES2_D3D9(),
                       ES2_D3D11(),
                       ES2_OPENGL(),
                       ES2_D3D11_FL9_3(),
                       ES2_VULKAN());
ANGLE_INSTANTIATE_TEST(StateChangeTestES3, ES3_D3D11(), ES3_OPENGL());
ANGLE_INSTANTIATE_TEST(SimpleStateChangeTest, ES2_VULKAN(), ES2_OPENGL());
ANGLE_INSTANTIATE_TEST(SimpleStateChangeTestES3, ES3_OPENGL(), ES3_D3D11());
ANGLE_INSTANTIATE_TEST(ValidationStateChangeTest, ES3_D3D11(), ES3_OPENGL());
ANGLE_INSTANTIATE_TEST(WebGL2ValidationStateChangeTest, ES3_D3D11(), ES3_OPENGL());
ANGLE_INSTANTIATE_TEST(ValidationStateChangeTestES31, ES31_OPENGL());
