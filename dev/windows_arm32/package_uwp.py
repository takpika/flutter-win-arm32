#!/usr/bin/env python3
"""Create UWP branding and an Appx manifest from application metadata."""
from pathlib import Path
import re
import struct
import uuid
import xml.etree.ElementTree as ET
from PIL import Image, ImageOps

FOUNDATION = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
UAP = 'http://schemas.microsoft.com/appx/manifest/uap/windows10'
PHONE = 'http://schemas.microsoft.com/appx/2014/phone/manifest'
FOUNDATION_CAPABILITIES = frozenset({
    'internetClient', 'internetClientServer', 'privateNetworkClientServer',
    'allJoyn', 'codeGeneration',
})
UAP_CAPABILITIES = frozenset({
    'documentsLibrary', 'picturesLibrary', 'videosLibrary', 'musicLibrary',
    'enterpriseAuthentication', 'sharedUserCertificates', 'userAccountInformation',
    'removableStorage', 'appointments', 'contacts', 'phoneCall',
    'blockedChatMessages', 'objects3D', 'voipCall', 'chat',
})
for prefix, namespace in [('', FOUNDATION), ('uap', UAP), ('mp', PHONE)]:
    ET.register_namespace(prefix, namespace)


def create_assets_and_manifest(bundle, binary_name, metadata, icon_path):
    requested = set(metadata.get('capabilities', ['internetClientServer', 'privateNetworkClientServer']))
    unknown = requested - FOUNDATION_CAPABILITIES - UAP_CAPABILITIES
    if unknown:
        raise ValueError('Unsupported Appx capability schema: ' + ', '.join(sorted(unknown)))
    uses_uap_capability = bool(requested & UAP_CAPABILITIES)
    minimum = metadata.get('minVersion', '10.0.10586.0' if uses_uap_capability else '10.0.10240.0')
    if uses_uap_capability and tuple(map(int, minimum.split('.'))) < (10, 0, 10586, 0):
        raise ValueError('UAP capabilities require minVersion 10.0.10586.0 or later')
    identity = metadata['identityName']
    if not re.fullmatch(r'[A-Za-z0-9.-]{3,50}', identity):
        raise ValueError('Appx identityName must contain 3 to 50 letters, digits, dots or hyphens')
    version = metadata['version'].split('.')
    if len(version) != 4 or any(not value.isdecimal() or int(value) > 65535 for value in version):
        raise ValueError('Appx version must have four numeric components from 0 to 65535')
    background = metadata.get('backgroundColor', '#FFFFFF')
    if not re.fullmatch(r'#[0-9a-fA-F]{6}', background):
        raise ValueError('backgroundColor must be #RRGGBB')
    color = tuple(int(background[index:index + 2], 16) for index in (1, 3, 5)) + (255,)
    with Image.open(icon_path) as source:
        if source.format == 'ICO':
            source = source.ico.getimage(max(source.ico.sizes(), key=lambda size: size[0] * size[1]))
        icon = source.convert('RGBA')
    assets = bundle / 'Assets'
    assets.mkdir(parents=True, exist_ok=True)
    def image(name, size, logo_size, opaque=False):
        canvas = Image.new('RGBA', size, color if opaque else (0, 0, 0, 0))
        logo = ImageOps.contain(icon, (logo_size, logo_size), Image.Resampling.LANCZOS)
        canvas.alpha_composite(logo, ((size[0] - logo.width) // 2, (size[1] - logo.height) // 2))
        canvas.save(assets / name)
        return canvas
    image('StoreLogo.png', (50, 50), 50)
    image('AppListLogo.png', (44, 44), 32)
    image('TileLogo.png', (150, 150), 70)
    image('SmallTileLogo.png', (71, 71), 38)
    image('WideTileLogo.png', (310, 150), 70)
    splash = image('PhoneSplash.png', (480, 800), 150, opaque=True)
    # Pixels are opaque after compositing; their RGBA values are also valid
    # premultiplied values for the native GL_ONE blend path.
    (assets / 'FlutterSplash.bin').write_bytes(
        struct.pack('<4sII4B', b'FSP1', splash.width, splash.height, *color) + splash.tobytes())
    package = ET.Element(f'{{{FOUNDATION}}}Package', {'IgnorableNamespaces': 'uap mp'})
    def element(parent, name, attributes=None, text=None, namespace=FOUNDATION):
        node = ET.SubElement(parent, f'{{{namespace}}}{name}', attributes or {})
        node.text = text
        return node
    element(package, 'Identity', {'Name': identity, 'Version': metadata['version'],
            'Publisher': metadata['publisher'], 'ProcessorArchitecture': 'arm'})
    element(package, 'PhoneIdentity', {
        'PhoneProductId': metadata.get('phoneProductId', str(uuid.uuid5(uuid.NAMESPACE_URL, 'flutter:windows-phone:' + identity))),
        'PhonePublisherId': metadata.get('phonePublisherId', '00000000-0000-0000-0000-000000000000')}, namespace=PHONE)
    properties = element(package, 'Properties')
    element(properties, 'DisplayName', text=metadata['displayName'])
    element(properties, 'PublisherDisplayName', text=metadata['publisherDisplayName'])
    element(properties, 'Logo', text='Assets/StoreLogo.png')
    dependencies = element(package, 'Dependencies')
    element(dependencies, 'TargetDeviceFamily', {'Name': 'Windows.Mobile',
            'MinVersion': minimum,
            'MaxVersionTested': metadata.get('maxVersionTested', '10.0.15254.0')})
    resources = element(package, 'Resources')
    for language in metadata.get('languages', ['en-US']):
        element(resources, 'Resource', {'Language': language})
    apps = element(package, 'Applications')
    app = element(apps, 'Application', {'Id': 'App', 'Executable': binary_name + '.exe', 'EntryPoint': identity + '.App'})
    visual = element(app, 'VisualElements', {'DisplayName': metadata['displayName'],
        'Description': metadata.get('description', metadata['displayName']), 'BackgroundColor': background,
        'Square150x150Logo': 'Assets/TileLogo.png', 'Square44x44Logo': 'Assets/AppListLogo.png'}, namespace=UAP)
    tile = element(visual, 'DefaultTile', {'Square71x71Logo': 'Assets/SmallTileLogo.png',
        'Wide310x150Logo': 'Assets/WideTileLogo.png', 'ShortName': metadata['displayName']}, namespace=UAP)
    names = element(tile, 'ShowNameOnTiles', namespace=UAP)
    element(names, 'ShowOn', {'Tile': 'square150x150Logo'}, namespace=UAP)
    element(names, 'ShowOn', {'Tile': 'wide310x150Logo'}, namespace=UAP)
    element(visual, 'SplashScreen', {'Image': 'Assets/PhoneSplash.png', 'BackgroundColor': background}, namespace=UAP)
    capabilities = element(package, 'Capabilities')
    # The engine maps precompiled ELF instruction pages through the documented
    # VirtualProtectFromApp API; UWP requires this package capability.
    required = {'codeGeneration'}
    for capability in sorted((required | requested) & FOUNDATION_CAPABILITIES):
        element(capabilities, 'Capability', {'Name': capability})
    for capability in sorted(requested & UAP_CAPABILITIES):
        element(capabilities, 'Capability', {'Name': capability}, namespace=UAP)
    ET.indent(package)
    ET.ElementTree(package).write(bundle / 'AppxManifest.xml', encoding='utf-8', xml_declaration=True)
