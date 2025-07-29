//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//

#include <MaterialXGenGlsl/Nodes/DisplacementNodeGlsl.h>

#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/HwShaderGenerator.h>
#include <iostream>

MATERIALX_NAMESPACE_BEGIN

ShaderNodeImplPtr DisplacementNodeGlsl::create()
{
    return std::make_shared<DisplacementNodeGlsl>();
}

void DisplacementNodeGlsl::createVariables(const ShaderNode& node, GenContext&, Shader& shader) const
{
    ShaderStage& vs = shader.getStage(Stage::VERTEX);
    ShaderStage& ps = shader.getStage(Stage::PIXEL);

    // Add displacement variable to vertex stage output
    addStageConnector(HW::VERTEX_DATA, Type::DISPLACEMENTSHADER, node.getOutput()->getVariable(), vs, ps);
}

void DisplacementNodeGlsl::emitFunctionCall(const ShaderNode& node, GenContext& context, ShaderStage& stage) const
{
    const HwShaderGenerator& shadergen = static_cast<const HwShaderGenerator&>(context.getShaderGenerator());

    DEFINE_SHADER_STAGE(stage, Stage::VERTEX)
    {
        // Add console logging to understand what's happening
        std::cout << "DEBUG: DisplacementNodeGlsl::emitFunctionCall called for vertex stage" << std::endl;
        std::cout << "DEBUG: Processing displacement node: " + node.getName() << std::endl;
        std::cout << "DEBUG: Node has " << node.getInputs().size() << " inputs" << std::endl;
        
        // CRITICAL FIX: Emit all connected/dependent nodes FIRST before constructing the displacement shader
        std::cout << "DEBUG: Emitting dependent function calls for all connected nodes" << std::endl;
        shadergen.emitDependentFunctionCalls(node, context, stage);
        std::cout << "DEBUG: Completed emitDependentFunctionCalls - checking if variables are now available" << std::endl;
        
        // Process each input with detailed checking
        for (ShaderInput* input : node.getInputs())
        {
            std::cout << "DEBUG: Input " << input->getName() << " of type " << input->getType().getName() << std::endl;
            if (input->getConnection())
            {
                const ShaderNode* connectedNode = input->getConnection()->getNode();
                std::cout << "DEBUG: Input connected to node: " << connectedNode->getName() << std::endl;
            }
            else
            {
                std::cout << "DEBUG: Input " << input->getName() << " has no connection (constant/default value)" << std::endl;
            }
        }
        
        // Now emit the displacement shader construction - connected nodes should now be available
        const ShaderOutput* output = node.getOutput();
        std::cout << "DEBUG: About to emit displacement shader construction" << std::endl;
        shadergen.emitLineBegin(stage);
        shadergen.emitOutput(output, true, false, context, stage);
        shadergen.emitString(" = displacementshader(", stage);
        
        // Handle displacement input (should be vec3 or float)
        const ShaderInput* dispInput = node.getInput("displacement");
        if (dispInput)
        {
            std::cout << "DEBUG: Emitting displacement input" << std::endl;
            if (dispInput->getType() == Type::FLOAT)
            {
                // Convert float displacement to vec3 by making it a normal displacement
                shadergen.emitString("vec3(0.0, 0.0, ", stage);
                shadergen.emitInput(dispInput, context, stage);
                shadergen.emitString(")", stage);
            }
            else
            {
                // Use vec3 displacement directly
                shadergen.emitInput(dispInput, context, stage);
            }
        }
        else
        {
            std::cout << "DEBUG: Using default displacement: vec3(0.0)" << std::endl;
            shadergen.emitString("vec3(0.0)", stage);
        }
        
        shadergen.emitString(", ", stage);
        
        // Handle scale input
        const ShaderInput* scaleInput = node.getInput("scale");
        if (scaleInput)
        {
            std::cout << "DEBUG: Emitting scale input" << std::endl;
            shadergen.emitInput(scaleInput, context, stage);
        }
        else
        {
            std::cout << "DEBUG: Using default scale: 1.0" << std::endl;
            shadergen.emitString("1.0", stage);
        }
        
        shadergen.emitString(")", stage);
        shadergen.emitLineEnd(stage);
        
        // Store displacement result in vertex data for pixel stage
        VariableBlock& vertexData = stage.getOutputBlock(HW::VERTEX_DATA);
        const string prefix = shadergen.getVertexDataPrefix(vertexData);
        ShaderPort* displacementPort = vertexData[output->getVariable()];
        if (displacementPort && !displacementPort->isEmitted())
        {
            displacementPort->setEmitted();
            shadergen.emitLine(prefix + displacementPort->getVariable() + " = " + output->getVariable(), stage);
        }
    }

    DEFINE_SHADER_STAGE(stage, Stage::PIXEL)
    {
        std::cout << "DEBUG: DisplacementNodeGlsl::emitFunctionCall called for pixel stage" << std::endl;
        // In pixel stage, just read the displacement from vertex data
        VariableBlock& vertexData = stage.getInputBlock(HW::VERTEX_DATA);
        const string prefix = shadergen.getVertexDataPrefix(vertexData);
        const ShaderOutput* output = node.getOutput();
        const ShaderPort* displacementPort = vertexData[output->getVariable()];
        if (displacementPort)
        {
            shadergen.emitLineBegin(stage);
            shadergen.emitOutput(output, true, false, context, stage);
            shadergen.emitString(" = " + prefix + displacementPort->getVariable(), stage);
            shadergen.emitLineEnd(stage);
        }
    }
}

MATERIALX_NAMESPACE_END
