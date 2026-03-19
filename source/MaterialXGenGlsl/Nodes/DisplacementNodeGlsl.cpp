//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXGenGlsl/Nodes/DisplacementNodeGlsl.h>

#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/HwShaderGenerator.h>

MATERIALX_NAMESPACE_BEGIN

ShaderNodeImplPtr DisplacementNodeGlsl::create()
{
    return std::make_shared<DisplacementNodeGlsl>();
}

void DisplacementNodeGlsl::createVariables(const ShaderNode& node, GenContext&, Shader& shader) const
{
    ShaderStage& vs = shader.getStage(Stage::VERTEX);
    ShaderStage& ps = shader.getStage(Stage::PIXEL);

    // Add displacement as a vertex-to-pixel connector so the pixel stage
    // can access the computed displacement if needed.
    addStageConnector(HW::VERTEX_DATA, Type::DISPLACEMENTSHADER, node.getOutput()->getVariable(), vs, ps);
}

void DisplacementNodeGlsl::emitFunctionCall(const ShaderNode& node, GenContext& context, ShaderStage& stage) const
{
    const HwShaderGenerator& shadergen = static_cast<const HwShaderGenerator&>(context.getShaderGenerator());

    DEFINE_SHADER_STAGE(stage, Stage::VERTEX)
    {
        // Emit all dependent nodes first (fractal3d, position, multiply, etc.)
        // so their outputs are available for the displacement calculation.
        shadergen.emitDependentFunctionCalls(node, context, stage);

        // Construct the displacementshader struct from inputs.
        const ShaderOutput* output = node.getOutput();
        shadergen.emitLineBegin(stage);
        shadergen.emitOutput(output, true, false, context, stage);
        shadergen.emitString(" = displacementshader(", stage);

        // Displacement input (float → vec3 conversion, or vec3 directly)
        const ShaderInput* dispInput = node.getInput("displacement");
        if (dispInput)
        {
            if (dispInput->getType() == Type::FLOAT)
            {
                // Float displacement along normal direction (Z in tangent space)
                shadergen.emitString("vec3(0.0, 0.0, ", stage);
                shadergen.emitInput(dispInput, context, stage);
                shadergen.emitString(")", stage);
            }
            else
            {
                shadergen.emitInput(dispInput, context, stage);
            }
        }
        else
        {
            shadergen.emitString("vec3(0.0)", stage);
        }

        shadergen.emitString(", ", stage);

        // Scale input
        const ShaderInput* scaleInput = node.getInput("scale");
        if (scaleInput)
        {
            shadergen.emitInput(scaleInput, context, stage);
        }
        else
        {
            shadergen.emitString("1.0", stage);
        }

        shadergen.emitString(")", stage);
        shadergen.emitLineEnd(stage);

        // Pass displacement to pixel stage via vertex data connector.
        VariableBlock& vertexData = stage.getOutputBlock(HW::VERTEX_DATA);
        const string prefix = shadergen.getVertexDataPrefix(vertexData);
        ShaderPort* port = vertexData[output->getVariable()];
        if (port && !port->isEmitted())
        {
            port->setEmitted();
            shadergen.emitLine(prefix + port->getVariable() + " = " + output->getVariable(), stage);
        }
    }

    DEFINE_SHADER_STAGE(stage, Stage::PIXEL)
    {
        // In the pixel stage, read displacement from the vertex data connector.
        VariableBlock& vertexData = stage.getInputBlock(HW::VERTEX_DATA);
        const string prefix = shadergen.getVertexDataPrefix(vertexData);
        const ShaderOutput* output = node.getOutput();
        const ShaderPort* port = vertexData[output->getVariable()];
        if (port)
        {
            shadergen.emitLineBegin(stage);
            shadergen.emitOutput(output, true, false, context, stage);
            shadergen.emitString(" = " + prefix + port->getVariable(), stage);
            shadergen.emitLineEnd(stage);
        }
    }
}

MATERIALX_NAMESPACE_END
