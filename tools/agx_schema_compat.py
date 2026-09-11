"""Narrow compatibility for the pinned renderer and current AGX schemas."""


def install_start3d_helper_cfg_compatibility(start3d_struct_cls):
    """Make Construct read the key emitted by the pinned historical renderer."""

    fields = start3d_struct_cls.subcon.subcons
    documented = [field for field in fields if field.name == "helper_cfg"]
    historical = [field for field in fields if field.name == "unk_40"]
    if len(historical) == 1 and not documented:
        return
    if len(documented) != 1 or historical:
        raise RuntimeError("unexpected Start3DStruct1 helper field layout")
    documented[0].name = "unk_40"


def install_tiling_helper_cfg_default(render_module):
    """Initialize the helper field appended after the pinned renderer."""

    current = render_module.TilingParameters
    if getattr(current, "_capture_helper_cfg_default", False):
        return current
    fields = [field for field in current.subcon.subcons if field.name == "helper_cfg"]
    if len(fields) != 1:
        raise RuntimeError("unexpected TilingParameters helper field layout")

    class CompatibleTilingParameters(current):
        _capture_helper_cfg_default = True

        def __init__(self):
            super().__init__()
            self.helper_cfg = 0

    CompatibleTilingParameters.__name__ = current.__name__
    CompatibleTilingParameters.__qualname__ = current.__qualname__
    render_module.TilingParameters = CompatibleTilingParameters
    return CompatibleTilingParameters


def install_work_command_ta_padding_compatibility(render_module):
    """Keep the historical TA writer aligned with the appended helper field."""

    current = render_module.WorkCommandTA
    if getattr(current, "_capture_ta_padding_compatibility", False):
        return current
    fields = [field for field in current.subcon.subcons if field.name == "unk_3e8"]
    if len(fields) != 1 or fields[0].sizeof() != 0x60:
        raise RuntimeError("unexpected WorkCommandTA padding layout")

    class CompatibleWorkCommandTA(current):
        _capture_ta_padding_compatibility = True

        def __setattr__(self, name, value):
            if name == "unk_3e8":
                if value == bytes(0x64):
                    value = bytes(0x60)
                elif value != bytes(0x60):
                    raise RuntimeError("unexpected historical WorkCommandTA padding")
            super().__setattr__(name, value)

    CompatibleWorkCommandTA.__name__ = current.__name__
    CompatibleWorkCommandTA.__qualname__ = current.__qualname__
    render_module.WorkCommandTA = CompatibleWorkCommandTA
    return CompatibleWorkCommandTA


def install_historical_renderer_schema_compatibility(render_module, start3d_struct_cls):
    """Install the complete fail-closed schema bridge required by the renderer."""

    install_start3d_helper_cfg_compatibility(start3d_struct_cls)
    install_tiling_helper_cfg_default(render_module)
    install_work_command_ta_padding_compatibility(render_module)
