#!/usr/bin/env python
'''
Render a MaterialX document to an image using OSL testrender (pathtracing).

Produces ground-truth pathtraced renders comparable to MaterialXView's
real-time output. Default camera/lighting matches the MaterialX test
suite's canonical render setup (sphere at origin, eye at (0,0,3), FOV
79.334°, san_giuseppe_bridge environment).

Note: testrender uses geometric primitives (sphere), not the shaderball
mesh that MaterialXView uses. The framing is matched to the test suite.

Usage:
    python renderosl.py material.mtlx -o output.png
    python renderosl.py material.mtlx --rays 32           # High quality
    python renderosl.py material.mtlx --screenColor 0.5,0.5,0.5  # Custom bg

Requirements:
    - oslc (OSL compiler) and testrender on PATH or via --oslc / --testrender
    - MaterialX Python bindings (pip install MaterialX or build from source)
'''

import sys
import os
import argparse
import shutil
import subprocess
import tempfile

import MaterialX as mx
import MaterialX.PyMaterialXGenOsl as mx_gen_osl
import MaterialX.PyMaterialXGenShader as mx_gen_shader
import MaterialX.PyMaterialXRender as mx_render


SCENE_TEMPLATE = '''\
<!-- Generated scene for testrender -->
<World>
   <Camera {camera_attrs} />

   <!-- Background environment map. -->
   <ShaderGroup>
      {env_overrides}
      shader envmap layer1;
   </ShaderGroup>
   <Background resolution="2048" />

   <!-- Background quad with raytype test. -->
   <ShaderGroup>
      color Cin {bg_r} {bg_g} {bg_b};
      {env_overrides}
      shader raytype_background layer1;
   </ShaderGroup>
   <Quad corner="-340, -340, -800" edge_x="680, 0, 0" edge_y="0, 680, 0" />

   <!-- Directional light (matches MaterialXView's san_giuseppe light rig) -->
   {direct_light}

   <!-- Material shader graph -->
   <ShaderGroup>
      {param_overrides};
      shader {input_shader} inputShader;
      shader {output_shader} outputShader;
      connect inputShader.{input_output} outputShader.{output_input};
   </ShaderGroup>

   {geometry}
</World>
'''


def find_executable(name, flag_path=None):
    '''Find an executable by explicit path, PATH, or common locations.'''
    if flag_path and os.path.isfile(flag_path):
        return flag_path
    found = shutil.which(name)
    if found:
        return found
    # Check common OSL install locations
    for prefix in ['/usr/local', os.path.expanduser('~/osl/dist'),
                   os.path.join(os.path.dirname(__file__), '..', '..', 'osl', 'dist')]:
        candidate = os.path.join(prefix, 'bin', name)
        if os.path.isfile(candidate):
            return candidate
    return None


def find_renderable(doc, target='genosl'):
    '''Find the first renderable element in the document.'''
    elems = mx_gen_shader.findRenderableElements(doc)
    for elem in elems:
        if elem.isA(mx.Node) and elem.getType() == 'material':
            inp = elem.getInput('surfaceshader')
            if inp:
                shader = inp.getConnectedNode()
                if shader:
                    return shader
        return elem
    return None


def generate_osl(doc, elem, stdlib, search_path, output_dir):
    '''Generate OSL shader code for a renderable element.'''
    gen = mx_gen_osl.OslShaderGenerator.create()
    context = mx_gen_shader.GenContext(gen)
    context.getOptions().fileTextureVerticalFlip = False

    # Register search paths for shader source
    context.registerSourceCodeSearchPath(search_path)

    shader_name = elem.getName()
    shader = gen.generate(shader_name, elem, context)

    # Write source stages
    pixel_source = shader.getSourceCode(mx_gen_shader.PIXEL_STAGE)
    osl_path = os.path.join(output_dir, shader_name + '.osl')
    with open(osl_path, 'w') as f:
        f.write(pixel_source)

    # Determine output type
    outputs = elem.getOutputs() if hasattr(elem, 'getOutputs') else []
    out_type = 'surfaceshader'
    if hasattr(elem, 'getType'):
        out_type = elem.getType()

    return shader_name, osl_path, out_type


def compile_osl(osl_path, oslc_exe, include_paths, output_dir):
    '''Compile .osl to .oso using oslc.'''
    oso_path = os.path.join(output_dir, os.path.splitext(os.path.basename(osl_path))[0] + '.oso')
    cmd = [oslc_exe]
    for inc in include_paths:
        # Split colon/semicolon-separated paths
        for p in inc.split(os.pathsep):
            p = p.strip()
            if p and os.path.isdir(p):
                cmd.append(f'-I{p}')
    cmd += ['-o', oso_path, osl_path]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'oslc command: {" ".join(cmd)}', file=sys.stderr)
        print(f'oslc failed:\n{result.stderr}', file=sys.stderr)
        sys.exit(1)
    return oso_path


def render_testrender(scene_file, output_file, width, height, rays,
                      oso_paths, testrender_exe):
    '''Run testrender to produce the final image.'''
    cmd = [testrender_exe, scene_file, output_file,
           '-r', str(width), str(height),
           '--path', ':'.join(oso_paths),
           '-aa', str(rays)]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'testrender failed:\n{result.stderr}', file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description='Render a MaterialX file to an image using OSL pathtracing (ground truth).',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='Examples:\n'
               '  %(prog)s material.mtlx -o render.png\n'
               '  %(prog)s material.mtlx --width 1024 --height 1024 --rays 32\n'
               '  %(prog)s material.mtlx --screenColor 0.3,0.3,0.32\n')

    parser.add_argument('material', help='Path to .mtlx file')
    parser.add_argument('-o', '--output', default=None,
                        help='Output image path (default: <material>_osl.png)')
    parser.add_argument('--width', type=int, default=512, help='Image width (default: 512)')
    parser.add_argument('--height', type=int, default=512, help='Image height (default: 512)')
    parser.add_argument('--rays', type=int, default=4,
                        help='Rays per pixel for anti-aliasing (default: 4, use 32+ for quality)')
    parser.add_argument('--screenColor', default='0.3,0.3,0.32',
                        help='Background color as R,G,B in sRGB (default: 0.3,0.3,0.32)')
    parser.add_argument('--fov', type=float, default=79.334,
                        help='Camera field of view in degrees (default: 79.334, matches MaterialXView)')
    parser.add_argument('--envRad', default=None,
                        help='Path to environment radiance HDR image (default: resources/Lights/san_giuseppe_bridge_split.hdr)')
    parser.add_argument('--mesh', default='shaderball', choices=['shaderball', 'sphere'],
                        help='Geometry to render (default: shaderball). '
                             '"shaderball" uses the same model as MaterialXView.')
    parser.add_argument('--path', dest='paths', action='append', nargs='+', default=[],
                        help='Additional search path for MaterialX libraries')
    parser.add_argument('--oslc', default=None, help='Path to oslc compiler')
    parser.add_argument('--testrender', default=None, help='Path to testrender executable')
    parser.add_argument('--keep-temp', action='store_true',
                        help='Keep temporary OSL/OSO/scene files for inspection')

    opts = parser.parse_args()

    # Find OSL executables
    oslc_exe = find_executable('oslc', opts.oslc)
    testrender_exe = find_executable('testrender', opts.testrender)
    if not oslc_exe:
        print('Error: oslc not found. Install OSL or use --oslc flag.', file=sys.stderr)
        sys.exit(1)
    if not testrender_exe:
        print('Error: testrender not found. Install OSL or use --testrender flag.', file=sys.stderr)
        sys.exit(1)

    print(f'Using oslc: {oslc_exe}')
    print(f'Using testrender: {testrender_exe}')

    # Load MaterialX document
    doc = mx.createDocument()
    search_path = mx.getDefaultDataSearchPath()
    for p in opts.paths:
        for pp in p:
            search_path.append(mx.FilePath(pp))

    # Add the material's directory to search path
    material_dir = os.path.dirname(os.path.abspath(opts.material))
    search_path.append(mx.FilePath(material_dir))

    stdlib = mx.createDocument()
    mx.loadLibraries(mx.getDefaultDataLibraryFolders(), search_path, stdlib)
    doc.importLibrary(stdlib)

    mx.readFromXmlFile(doc, opts.material, search_path)

    # Find renderable element
    elem = find_renderable(doc)
    if not elem:
        print('Error: No renderable element found in document.', file=sys.stderr)
        sys.exit(1)

    print(f'Rendering: {elem.getNamePath()} (type: {elem.getType()})')

    # Create temp directory for intermediate files
    if opts.keep_temp:
        tmp_dir = os.path.join(material_dir, '_osl_render')
        os.makedirs(tmp_dir, exist_ok=True)
    else:
        tmp_dir_obj = tempfile.mkdtemp(prefix='mx_osl_')
        tmp_dir = tmp_dir_obj

    try:
        # Generate OSL source
        shader_name, osl_path, out_type = generate_osl(
            doc, elem, stdlib, search_path, tmp_dir)
        print(f'Generated OSL: {osl_path}')

        # Find OSL utility shaders (closure_passthrough, etc.)
        # These are in the MaterialX test utilities directory
        mx_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        utilities_dir = os.path.join(mx_root, 'source', 'MaterialXTest',
                                      'MaterialXRenderOsl', 'Utilities')
        if not os.path.isdir(utilities_dir):
            # Try relative to the installed package
            utilities_dir = os.path.join(mx_root, 'resources', 'Utilities', 'OslRender')

        # Compile utility shaders
        utility_oso_dir = os.path.join(tmp_dir, 'utilities')
        os.makedirs(utility_oso_dir, exist_ok=True)

        # Include paths for oslc: MaterialX stdlib OSL headers + search paths
        mx_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        osl_include_paths = [
            os.path.join(mx_root, 'libraries', 'stdlib', 'genosl', 'include'),
            os.path.join(mx_root, 'libraries'),
            search_path.asString(),
        ]

        if os.path.isdir(utilities_dir):
            for util_file in os.listdir(utilities_dir):
                if util_file.endswith('.osl'):
                    util_path = os.path.join(utilities_dir, util_file)
                    compile_osl(util_path, oslc_exe, osl_include_paths, utility_oso_dir)
            print(f'Compiled utility shaders from: {utilities_dir}')
        else:
            print(f'Warning: Utility shaders not found at {utilities_dir}', file=sys.stderr)

        # Compile the material shader
        oso_path = compile_osl(osl_path, oslc_exe, osl_include_paths, tmp_dir)
        print(f'Compiled OSO: {oso_path}')

        # Determine output shader type
        is_closure = out_type == 'surfaceshader'
        output_shader = 'closure_passthrough' if is_closure else 'constant_color'
        output_input = 'Cin'
        input_output = 'out'

        # Parse background color (sRGB to linear for OSL)
        bg = [float(x) for x in opts.screenColor.split(',')]
        # sRGB to linear conversion
        def srgb_to_linear(c):
            return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
        bg_lin = [srgb_to_linear(c) for c in bg]

        # Find environment map
        env_rad = opts.envRad
        if not env_rad:
            env_rad = os.path.join(mx_root, 'resources', 'Lights', 'san_giuseppe_bridge_split.hdr')
        env_overrides = ''
        if env_rad and os.path.isfile(env_rad):
            env_overrides = f'string envmap_filename "{env_rad}";'

        # Set up geometry and camera based on mesh choice
        if opts.mesh == 'shaderball':
            shaderball_obj = os.path.join(mx_root, 'resources', 'Geometry', 'shaderball.obj')
            if not os.path.isfile(shaderball_obj):
                print(f'Warning: shaderball.obj not found at {shaderball_obj}, falling back to sphere',
                      file=sys.stderr)
                opts.mesh = 'sphere'
            else:
                # Use the mesh as-is (no vertex transform) so procedural shaders
                # that use object-space position produce correct patterns.
                # Position the camera to match MaterialXView's framing instead.
                geometry = f'<Model filename="{shaderball_obj}" />'

                # MaterialXView: camera at (0,0,5), FOV 45°, mesh centered via
                # worldMatrix = translate(-center) * scale(2.0/radius).
                # Equivalent untransformed view: camera at center + (0,0,dist)
                # where dist = 5.0 / meshScale, looking at center.
                import math
                min_xyz = [float('inf')] * 3
                max_xyz = [float('-inf')] * 3
                with open(shaderball_obj) as f:
                    for line in f:
                        if line.startswith('v ') and not line.startswith('vt') and not line.startswith('vn'):
                            parts = line.split()
                            for i in range(3):
                                v = float(parts[i + 1])
                                min_xyz[i] = min(min_xyz[i], v)
                                max_xyz[i] = max(max_xyz[i], v)
                center = [(a + b) / 2 for a, b in zip(min_xyz, max_xyz)]
                import math
                bbox_radius = math.sqrt(sum((c - m) ** 2 for c, m in zip(center, min_xyz)))
                mesh_scale = 2.0 / bbox_radius  # IDEAL_MESH_SPHERE_RADIUS / radius
                cam_dist = 5.0 / mesh_scale
                # testrender's fov parameter maps to half the screen, so the
                # effective vertical FOV = 2*atan(tan(fov/2)/2). To match
                # MaterialXView's 45° vertical FOV we need fov ≈ 79.28°.
                import math
                effective_half_fov = math.radians(45.0 / 2)
                tr_fov = 2 * math.degrees(math.atan(2 * math.tan(effective_half_fov)))
                camera_attrs = (f'eye="{center[0]:.4f}, {center[1]:.4f}, {center[2] + cam_dist:.4f}" '
                                f'look_at="{center[0]:.4f}, {center[1]:.4f}, {center[2]:.4f}" '
                                f'fov="{tr_fov:.3f}"')

        if opts.mesh == 'sphere':
            geometry = '<Sphere center="0, 0, 0" radius="1" />'
            camera_attrs = f'eye="0, 0, 3" dir="0, 0, -1" fov="{opts.fov}"'

        # Set up directional light from the light rig .mtlx file
        # (MaterialXView loads this automatically from <envmap>.mtlx)
        direct_light = ''
        if env_rad:
            light_rig_path = os.path.splitext(env_rad)[0] + '.mtlx'
            if os.path.isfile(light_rig_path):
                import math as _m
                light_doc = mx.createDocument()
                mx.readFromXmlFile(light_doc, light_rig_path)
                for node in light_doc.getNodes():
                    if node.getCategory() == 'directional_light':
                        dir_input = node.getInput('direction')
                        col_input = node.getInput('color')
                        int_input = node.getInput('intensity')
                        if dir_input and col_input:
                            d = [float(x) for x in dir_input.getValueString().split(',')]
                            c = [float(x) for x in col_input.getValueString().split(',')]
                            intensity = float(int_input.getValueString()) if int_input else 1.0
                            # Place an emissive sphere in the light direction.
                            # A sphere produces a round specular highlight matching
                            # MaterialXView's point/directional light reflection.
                            dist = 20.0
                            light_radius = 1.5
                            # Normalize direction
                            mag = _m.sqrt(sum(x*x for x in d))
                            dn = [-x/mag for x in d]  # toward light
                            pos = [dn[i] * dist for i in range(3)]
                            # emitter: radiance = power / (PI * surfacearea)
                            # surfacearea of sphere = 4 * PI * r^2
                            # irradiance at scene ≈ radiance * solid_angle
                            #   ≈ (power / (PI * 4*PI*r^2)) * (PI*r^2 / dist^2)
                            #   = power / (4 * PI * dist^2)
                            # Set irradiance = intensity:
                            #   power = intensity * 4 * PI * dist^2
                            power = intensity * 4 * _m.pi * dist * dist
                            direct_light = (
                                f'<ShaderGroup is_light="yes">\n'
                                f'      float power {power:.1f};\n'
                                f'      color Cs {c[0]} {c[1]} {c[2]};\n'
                                f'      shader emitter layer1;\n'
                                f'   </ShaderGroup>\n'
                                f'   <Sphere center="{pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f}" '
                                f'radius="{light_radius}" />'
                            )
                            break

        # Generate scene file
        scene_content = SCENE_TEMPLATE.format(
            camera_attrs=camera_attrs,
            env_overrides=env_overrides,
            bg_r=bg_lin[0], bg_g=bg_lin[1], bg_b=bg_lin[2],
            param_overrides='',
            input_shader=shader_name,
            output_shader=output_shader,
            input_output=input_output,
            output_input=output_input,
            geometry=geometry,
            direct_light=direct_light,
        )
        scene_file = os.path.join(tmp_dir, 'scene.xml')
        with open(scene_file, 'w') as f:
            f.write(scene_content)

        # Determine output file
        output_file = opts.output or os.path.splitext(opts.material)[0] + '_osl.png'
        output_file = os.path.abspath(output_file)

        # Run testrender
        # Include OSL's built-in shader directory (for emitter, matte, etc.)
        osl_shader_dir = os.path.join(os.path.dirname(testrender_exe), '..', 'share', 'OSL', 'shaders')
        oso_paths = [tmp_dir, utility_oso_dir]
        if os.path.isdir(osl_shader_dir):
            oso_paths.append(os.path.abspath(osl_shader_dir))
        print(f'Rendering {opts.width}x{opts.height} @ {opts.rays} rays/pixel...')
        render_testrender(scene_file, output_file, opts.width, opts.height,
                          opts.rays, oso_paths, testrender_exe)

        print(f'Wrote: {output_file}')

    finally:
        if not opts.keep_temp and 'tmp_dir_obj' in dir():
            shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == '__main__':
    main()
