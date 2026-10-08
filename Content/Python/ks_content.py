"""Creates the game's materials in /Game/KS/Materials.

The game has no hand-made assets. This script builds four small materials from standard
material nodes; the C++ code makes dynamic instances of them and sets their parameters.
It runs automatically when the editor opens the project (init_unreal.py), and
install_iphone.command runs it before cooking, with the editor in commandlet mode:

    UnrealEditor Kubostrel.uproject -run=pythonscript -script=".../ks_content.py"

Existing materials are kept unless MATERIAL_VERSION changes.
"""

import unreal

MATERIAL_DIR = "/Game/KS/Materials"
MATERIAL_VERSION = "3"
VERSION_TAG = "KSMaterialVersion"

mel = unreal.MaterialEditingLibrary


def _asset_api():
    """Editor asset functions: the editor's own subsystem when it exists, else the older library."""
    if hasattr(unreal, "EditorAssetSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
        if subsystem is not None:
            return subsystem
    return unreal.EditorAssetLibrary


eal = _asset_api()


def _log(text):
    unreal.log("[Kubostrel] " + text)


class Graph:
    """Small helper around MaterialEditingLibrary that places nodes on a grid."""

    def __init__(self, material):
        self.material = material
        self.row = 0

    def node(self, cls, x=-400, y=None, **props):
        if y is None:
            y = self.row * 140
            self.row += 1
        expr = mel.create_material_expression(self.material, cls, x, y)
        if expr is None:
            raise RuntimeError("Could not create %s" % cls.__name__)
        for key, value in props.items():
            expr.set_editor_property(key, value)
        return expr

    def link(self, src, dst, dst_input="", src_output=""):
        if not mel.connect_material_expressions(src, src_output, dst, dst_input):
            raise RuntimeError("Could not connect %s -> %s.%s" % (src.get_name(), dst.get_name(), dst_input))

    def output(self, src, prop, src_output=""):
        if not mel.connect_material_property(src, src_output, prop):
            raise RuntimeError("Could not connect %s to %s" % (src.get_name(), prop))

    def scalar(self, name, value, x=-1600):
        return self.node(unreal.MaterialExpressionScalarParameter, x, parameter_name=name, default_value=value)

    def vector(self, name, color, x=-1600):
        return self.node(unreal.MaterialExpressionVectorParameter, x, parameter_name=name,
                         default_value=unreal.LinearColor(color[0], color[1], color[2], 1.0))

    def const(self, value, x=-1400):
        return self.node(unreal.MaterialExpressionConstant, x, r=value)

    def binary(self, cls, a, b, x=-800):
        """Two-input node. b may be a node or a number."""
        expr = self.node(cls, x)
        self.link(a, expr, "A")
        if isinstance(b, (int, float)):
            b = self.const(float(b), x - 200)
        self.link(b, expr, "B")
        return expr

    def mul(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionMultiply, a, b, x)

    def add(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionAdd, a, b, x)

    def sub(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionSubtract, a, b, x)

    def div(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionDivide, a, b, x)

    def max(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionMax, a, b, x)

    def dot(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionDotProduct, a, b, x)

    def append(self, a, b, x=-800):
        return self.binary(unreal.MaterialExpressionAppendVector, a, b, x)

    def unary(self, cls, a, x=-800):
        expr = self.node(cls, x)
        self.link(a, expr, "")
        return expr

    def mask(self, a, r=False, g=False, b=False, x=-800):
        expr = self.node(unreal.MaterialExpressionComponentMask, x, r=r, g=g, b=b, a=False)
        self.link(a, expr, "")
        return expr

    def lerp(self, a, b, alpha, x=-600):
        expr = self.node(unreal.MaterialExpressionLinearInterpolate, x)
        for src, pin in ((a, "A"), (b, "B"), (alpha, "Alpha")):
            if isinstance(src, (int, float)):
                src = self.const(float(src), x - 200)
            self.link(src, expr, pin)
        return expr


def _new_material(name):
    path = MATERIAL_DIR + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create " + path)
    # Arena pieces are drawn as instanced meshes; without this flag a cooked game shows the default material.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    return material


def _finish(material):
    mel.layout_material_expressions(material)
    mel.recompile_material(material)
    eal.set_metadata_tag(material, VERSION_TAG, MATERIAL_VERSION)
    eal.save_loaded_asset(material, False)


def build_surface():
    """Lit surface with a world-aligned grid. Grid lines can darken the base color and glow."""
    m = _new_material("M_KS_Surface")
    g = Graph(m)

    color = g.vector("Color", (0.5, 0.5, 0.5))
    roughness = g.scalar("Roughness", 0.6)
    metallic = g.scalar("Metallic", 0.0)
    grid_scale = g.scalar("GridScale", 200.0)
    grid_strength = g.scalar("GridStrength", 0.3)
    glow_color = g.vector("GlowColor", (0.0, 0.0, 0.0))
    grid_glow = g.scalar("GridGlow", 0.0)

    # Distance to the nearest grid line along each world axis: 1 on a line, 0 between lines.
    world_pos = g.node(unreal.MaterialExpressionWorldPosition, -1600)
    cells = g.div(world_pos, grid_scale, -1400)
    frac = g.unary(unreal.MaterialExpressionFrac, cells, -1250)
    centered = g.sub(frac, 0.5, -1100)
    dist = g.unary(unreal.MaterialExpressionAbs, centered, -950)
    edge = g.mul(dist, 2.0, -800)
    # Lines are about 4% of a cell wide with a soft edge.
    ramp = g.mul(g.sub(edge, 0.9, -700), 14.0, -600)
    lines = g.unary(unreal.MaterialExpressionSaturate, ramp, -500)

    lx = g.mask(lines, r=True, x=-400)
    ly = g.mask(lines, g=True, x=-400)
    lz = g.mask(lines, b=True, x=-400)
    # Each face shows the lines of the two axes that run along it.
    face_x = g.max(ly, lz, -300)
    face_y = g.max(lx, lz, -300)
    face_z = g.max(lx, ly, -300)
    per_face = g.append(g.append(face_x, face_y, -200), face_z, -100)

    normal = g.node(unreal.MaterialExpressionVertexNormalWS, -600)
    n_abs = g.unary(unreal.MaterialExpressionAbs, normal, -500)
    n2 = g.mul(n_abs, n_abs, -400)
    n4 = g.mul(n2, n2, -300)
    ones = g.node(unreal.MaterialExpressionConstant3Vector, -300, constant=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    weight_sum = g.dot(n4, ones, -200)
    weighted = g.dot(per_face, n4, -100)
    grid = g.div(weighted, weight_sum, 0)

    # Far away the thin lines would shimmer, so they fade to their average brightness.
    depth = g.node(unreal.MaterialExpressionPixelDepth, -400)
    fade = g.unary(unreal.MaterialExpressionSaturate, g.div(g.sub(depth, 1800.0, -300), 6000.0, -200), -100)
    grid_final = g.lerp(grid, 0.12, fade, 100)

    darken = g.unary(unreal.MaterialExpressionOneMinus, g.mul(grid_final, grid_strength, 200), 300)
    base = g.mul(color, darken, 400)
    glow = g.mul(g.mul(glow_color, grid_glow, 200), g.mul(grid_final, 3.0, 300), 400)
    rough = g.add(roughness, g.mul(grid_final, 0.15, 200), 400)

    g.output(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.output(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.output(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(metallic, unreal.MaterialProperty.MP_METALLIC)
    _finish(m)


def build_neon():
    """Unlit glowing strips: Color * Intensity."""
    m = _new_material("M_KS_Neon")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    g = Graph(m)
    color = g.vector("Color", (0.0, 0.8, 1.0))
    intensity = g.scalar("Intensity", 6.0)
    g.output(g.mul(color, intensity, -200), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(m)


def build_glow():
    """Additive glow for effects. Soft=1 fades the edges of round shapes, Opacity fades the whole thing."""
    m = _new_material("M_KS_Glow")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("two_sided", True)
    g = Graph(m)
    color = g.vector("Color", (1.0, 0.5, 0.1))
    intensity = g.scalar("Intensity", 4.0)
    opacity = g.scalar("Opacity", 1.0)
    soft = g.scalar("Soft", 1.0)

    normal = g.node(unreal.MaterialExpressionVertexNormalWS, -1200)
    camera = g.node(unreal.MaterialExpressionCameraVectorWS, -1200)
    facing = g.unary(unreal.MaterialExpressionAbs, g.dot(normal, camera, -1000), -900)
    facing2 = g.mul(facing, facing, -800)
    shape = g.lerp(1.0, facing2, soft, -600)
    strength = g.mul(g.mul(intensity, opacity, -500), shape, -400)
    g.output(g.mul(color, strength, -200), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(m)


def build_character():
    """Lit robot armor with a colored rim light and a white hit flash."""
    m = _new_material("M_KS_Character")
    g = Graph(m)
    color = g.vector("Color", (0.0, 0.7, 1.0))
    rim = g.scalar("Rim", 1.5)
    flash = g.scalar("Flash", 0.0)

    base = g.add(g.mul(color, 0.22, -800), 0.03, -600)
    fresnel = g.node(unreal.MaterialExpressionFresnel, -1000, exponent=3.0, base_reflect_fraction=0.02)
    rim_glow = g.mul(g.mul(color, rim, -800), fresnel, -600)
    inner_glow = g.mul(color, 0.12, -600)
    flash_glow = g.mul(flash, 4.0, -600)
    emissive = g.add(g.add(rim_glow, inner_glow, -400), flash_glow, -200)

    g.output(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.output(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.output(g.const(0.35, -400), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.const(0.3, -400), unreal.MaterialProperty.MP_METALLIC)
    _finish(m)


BUILDERS = {
    "M_KS_Surface": build_surface,
    "M_KS_Neon": build_neon,
    "M_KS_Glow": build_glow,
    "M_KS_Character": build_character,
}


def _is_current(name):
    path = MATERIAL_DIR + "/" + name
    if not eal.does_asset_exist(path):
        return False
    asset = eal.load_asset(path)
    return asset is not None and eal.get_metadata_tag(asset, VERSION_TAG) == MATERIAL_VERSION


def ensure_materials(force=False):
    if not eal.does_directory_exist(MATERIAL_DIR):
        eal.make_directory(MATERIAL_DIR)
    built = 0
    for name, builder in BUILDERS.items():
        if not force and _is_current(name):
            continue
        try:
            builder()
            built += 1
            _log("Created " + name)
        except Exception as error:  # Keep going: the game falls back to a plain material.
            unreal.log_error("[Kubostrel] Failed to create %s: %s" % (name, error))
    _log("Materials ready (%d rebuilt)" % built)
    return built


ensure_materials()
