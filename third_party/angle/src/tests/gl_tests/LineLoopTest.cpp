//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "test_utils/ANGLETest.h"

using namespace angle;

class LineLoopTest : public ANGLETest
{
  protected:
    LineLoopTest()
    {
        setWindowWidth(256);
        setWindowHeight(256);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    virtual void SetUp()
    {
        ANGLETest::SetUp();

        mProgram = CompileProgram(essl1_shaders::vs::Simple(), essl1_shaders::fs::UniformColor());
        if (mProgram == 0)
        {
            FAIL() << "shader compilation failed.";
        }

        mPositionLocation = glGetAttribLocation(mProgram, essl1_shaders::PositionAttrib());
        mColorLocation    = glGetUniformLocation(mProgram, essl1_shaders::ColorUniform());

        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        ASSERT_GL_NO_ERROR();
    }

    virtual void TearDown()
    {
        glDeleteProgram(mProgram);

        ANGLETest::TearDown();
    }

    void runTest(GLenum indexType, GLuint indexBuffer, const void *indexPtr)
    {
        glClear(GL_COLOR_BUFFER_BIT);

        static const GLfloat loopPositions[] = {0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.0f,
                                                0.0f,  0.0f, 0.0f, 0.0f, 0.0f, -0.5f, -0.5f,
                                                -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f};

        static const GLfloat stripPositions[] = {-0.5f, -0.5f, -0.5f, 0.5f,
                                                 0.5f,  0.5f,  0.5f,  -0.5f};
        static const GLubyte stripIndices[] = {1, 0, 3, 2, 1};

        glUseProgram(mProgram);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glEnableVertexAttribArray(mPositionLocation);
        glVertexAttribPointer(mPositionLocation, 2, GL_FLOAT, GL_FALSE, 0, loopPositions);
        glUniform4f(mColorLocation, 0.0f, 0.0f, 1.0f, 1.0f);
        glDrawElements(GL_LINE_LOOP, 4, indexType, indexPtr);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glVertexAttribPointer(mPositionLocation, 2, GL_FLOAT, GL_FALSE, 0, stripPositions);
        glUniform4f(mColorLocation, 0, 1, 0, 1);
        glDrawElements(GL_LINE_STRIP, 5, GL_UNSIGNED_BYTE, stripIndices);

        std::vector<GLubyte> pixels(getWindowWidth() * getWindowHeight() * 4);
        glReadPixels(0, 0, getWindowWidth(), getWindowHeight(), GL_RGBA, GL_UNSIGNED_BYTE,
                     &pixels[0]);

        ASSERT_GL_NO_ERROR();

        for (int y = 0; y < getWindowHeight(); y++)
        {
            for (int x = 0; x < getWindowWidth(); x++)
            {
                const GLubyte *pixel = &pixels[0] + ((y * getWindowWidth() + x) * 4);

                EXPECT_EQ(pixel[0], 0);
                EXPECT_EQ(pixel[1], pixel[2]);
                EXPECT_EQ(pixel[3], 255);
            }
        }
    }

    GLuint mProgram;
    GLint mPositionLocation;
    GLint mColorLocation;
};

TEST_P(LineLoopTest, LineLoopUByteIndices)
{
    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    // On Win7, the D3D SDK Layers emits a false warning for these tests.
    // This doesn't occur on Windows 10 (Version 1511) though.
    ignoreD3D11SDKLayersWarnings();

    static const GLubyte indices[] = {0, 7, 6, 9, 8, 0};
    runTest(GL_UNSIGNED_BYTE, 0, indices + 1);
}

TEST_P(LineLoopTest, LineLoopUShortIndices)
{
    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    ignoreD3D11SDKLayersWarnings();

    static const GLushort indices[] = {0, 7, 6, 9, 8, 0};
    runTest(GL_UNSIGNED_SHORT, 0, indices + 1);
}

TEST_P(LineLoopTest, LineLoopUIntIndices)
{
    if (!extensionEnabled("GL_OES_element_index_uint"))
    {
        return;
    }

    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    ignoreD3D11SDKLayersWarnings();

    static const GLuint indices[] = {0, 7, 6, 9, 8, 0};
    runTest(GL_UNSIGNED_INT, 0, indices + 1);
}

TEST_P(LineLoopTest, LineLoopUByteIndexBuffer)
{
    // TODO(jmadill): Diagnose and fix. http://anglebug.com/2802
    ANGLE_SKIP_TEST_IF(IsVulkan());

    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    ignoreD3D11SDKLayersWarnings();

    static const GLubyte indices[] = {0, 7, 6, 9, 8, 0};

    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    runTest(GL_UNSIGNED_BYTE, buf, reinterpret_cast<const void *>(sizeof(GLubyte)));

    glDeleteBuffers(1, &buf);
}

TEST_P(LineLoopTest, LineLoopUShortIndexBuffer)
{
    // TODO(fjhenigman): Probabe driver bug. Work around it and/or notify vendor.
    // http://anglebug.com/2838
    ANGLE_SKIP_TEST_IF(IsVulkan() && IsWindows() && IsIntel());

    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    ignoreD3D11SDKLayersWarnings();

    static const GLushort indices[] = {0, 7, 6, 9, 8, 0};

    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    runTest(GL_UNSIGNED_SHORT, buf, reinterpret_cast<const void *>(sizeof(GLushort)));

    glDeleteBuffers(1, &buf);
}

TEST_P(LineLoopTest, LineLoopUIntIndexBuffer)
{
    if (!extensionEnabled("GL_OES_element_index_uint"))
    {
        return;
    }

    // Disable D3D11 SDK Layers warnings checks, see ANGLE issue 667 for details
    ignoreD3D11SDKLayersWarnings();

    static const GLuint indices[] = {0, 7, 6, 9, 8, 0};

    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    runTest(GL_UNSIGNED_INT, buf, reinterpret_cast<const void *>(sizeof(GLuint)));

    glDeleteBuffers(1, &buf);
}

// Use this to select which configurations (e.g. which renderer, which GLES major version) these
// tests should be run against.
ANGLE_INSTANTIATE_TEST(LineLoopTest,
                       ES2_D3D9(),
                       ES2_D3D11(),
                       ES2_OPENGL(),
                       ES2_OPENGLES(),
                       ES2_VULKAN());
