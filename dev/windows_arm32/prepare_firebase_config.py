#!/usr/bin/env python3
"""Extract public Firebase client resources without editing the application."""
import argparse
import json
import os
from pathlib import Path
import re


def text(value, field):
    result = value.get(field)
    if not isinstance(result, str) or not result or any(ord(c) < 32 for c in result):
        raise ValueError('Missing or invalid Firebase client field: ' + field)
    return result


def certificate(value):
    if not isinstance(value, str):
        raise ValueError('Supply the original Android certificate SHA-1')
    value = value.replace(':', '').strip().lower()
    if not re.fullmatch('[0-9a-f]{40}', value):
        raise ValueError('Android certificate SHA-1 must contain 40 hexadecimal digits')
    return value


def prepare(application_root, configuration=None, *, messaging=False, display_name='Flutter app'):
    application_root = Path(application_root).resolve()
    if configuration:
        configuration = Path(configuration).resolve()
        source = json.loads(configuration.read_text(encoding='utf-8'))
        if not isinstance(source, dict) or set(source) != {'version', 'apps'} or type(source['version']) is not int or source['version'] != 1 or not isinstance(source['apps'], list):
            raise ValueError('Use a version 1 public Firebase client configuration')
        entries = source['apps']
        base = configuration.parent
    else:
        google = application_root / 'android/app/google-services.json'
        if not google.is_file():
            if messaging:
                raise ValueError('Firebase Messaging needs android/app/google-services.json or FLUTTER_WINDOWS_PHONE_FIREBASE_CONFIG')
            return None
        entries = [{'appName': '[DEFAULT]', 'googleServicesFile': str(google)}]
        base = application_root
    result, names = [], set()
    for entry in entries:
        allowed = {'appName', 'googleServicesFile', 'androidPackage', 'certificateSha1',
                   'certificateSha1File', 'coreOptions'}
        if not isinstance(entry, dict) or set(entry) - allowed:
            raise ValueError('Firebase input accepts public client configuration fields only')
        name = text(entry, 'appName')
        if name in names:
            raise ValueError('Duplicate Firebase application name')
        names.add(name)
        google = base / text(entry, 'googleServicesFile')
        services = json.loads(google.read_text(encoding='utf-8'))
        if 'private_key' in services or services.get('type') == 'service_account':
            raise ValueError('Use Android public client configuration, never a service account')
        project = services['project_info']
        clients = services['client']
        if 'androidPackage' in entry:
            clients = [c for c in clients if c['client_info']['android_client_info']['package_name'] == entry['androidPackage']]
        if len(clients) != 1:
            raise ValueError('Select exactly one Android application package in the Firebase configuration')
        client = clients[0]
        package = text(client['client_info']['android_client_info'], 'package_name')
        api = text(client['api_key'][0], 'current_key')
        app = text(client['client_info'], 'mobilesdk_app_id')
        project_id = text(project, 'project_id')
        number = project.get('project_number')
        sender = str(number) if type(number) is int else text(project, 'project_number')
        core = {'apiKey': api, 'appId': app, 'projectId': project_id, 'messagingSenderId': sender}
        for original, target in [('firebase_url', 'databaseURL'), ('storage_bucket', 'storageBucket')]:
            if original in project:
                core[target] = text(project, original)
        if 'coreOptions' in entry:
            options = entry['coreOptions']
            allowed_options = {'apiKey', 'appId', 'projectId', 'messagingSenderId', 'authDomain',
                               'databaseURL', 'storageBucket', 'measurementId', 'trackingId',
                               'deepLinkURLScheme', 'androidClientId', 'iosClientId', 'iosBundleId', 'appGroupId'}
            if not isinstance(options, dict) or set(options) - allowed_options:
                raise ValueError('Invalid native Firebase Core resource options')
            core = {k: text(options, k) for k in options if options[k] is not None}
            for key in ('apiKey', 'appId', 'projectId', 'messagingSenderId'):
                text(core, key)
            if core['projectId'] != project_id:
                raise ValueError('Core and Messaging client resources must use the same Firebase project')
        item = {'appName': name, 'coreOptions': core, 'displayName': display_name}
        if messaging:
            if 'certificateSha1' in entry and 'certificateSha1File' in entry:
                raise ValueError('Choose one Android certificate SHA-1 source')
            sha1 = entry.get('certificateSha1')
            if 'certificateSha1File' in entry:
                sha1 = (base / text(entry, 'certificateSha1File')).read_text(encoding='utf-8').strip()
            if sha1 is None:
                certificates = {certificate(c['android_info']['certificate_hash'])
                                for c in client.get('oauth_client', [])
                                if c.get('client_type') == 1 and c.get('android_info', {}).get('package_name') == package}
                if len(certificates) != 1:
                    raise ValueError('Supply the original Android certificate SHA-1 in FLUTTER_WINDOWS_PHONE_FIREBASE_CONFIG')
                sha1 = certificates.pop()
            item['messaging'] = {'apiKey': api, 'appId': app, 'projectId': project_id,
                                 'senderId': sender, 'androidPackage': package,
                                 'certificateSha1': certificate(sha1)}
        result.append(item)
    if not result:
        raise ValueError('Firebase client configuration must contain an application')
    return {'version': 1, 'apps': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--configuration', type=Path,
                        default=os.environ.get('FLUTTER_WINDOWS_PHONE_FIREBASE_CONFIG'))
    parser.add_argument('--messaging', action='store_true')
    parser.add_argument('--display-name', default='Flutter app')
    args = parser.parse_args()
    result = prepare(args.application_root, args.configuration, messaging=args.messaging,
                     display_name=args.display_name)
    if result is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
