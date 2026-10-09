"""Validate the shared SDK's header/import-only consumer distribution."""
import hashlib
import json


REQUIRED_ABI = 22

def load(root):
    directory = root / 'dev/windows_arm32/third_party/firebase_compat'
    manifest = directory / 'files.json'
    if hashlib.sha256(manifest.read_bytes()).hexdigest() != \
            'f07cedbf61fe540af175692f8b7e48b633e7118cfb1b049cf419451256ff1a54':
        raise ValueError('Changed Firebase consumer manifest')
    records = json.loads(manifest.read_text())['files']
    expected = {record['path'] for record in records}
    actual = {str(p.relative_to(directory)) for p in directory.rglob('*')
              if p.is_file() and p != manifest}
    if actual != expected:
        raise ValueError('Unexpected Firebase consumer files')
    for record in records:
        path = directory / record['path']
        if (not path.resolve().is_relative_to(directory.resolve()) or
                path.stat().st_size != record['bytes'] or
                hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']):
            raise ValueError('Changed Firebase consumer input: ' + record['path'])
    contract = json.loads((directory / 'consumer.json').read_text())
    if (contract['architecture'] != 'arm' or contract['embedDll'] or
            not contract['dependenciesInstalledSeparately']):
        raise ValueError('Expected an ARM32 Framework consumer, without a DLL body')
    if (contract['abi']['provided'] < REQUIRED_ABI or
            contract['abi']['minimumCompatible'] > REQUIRED_ABI or
            not contract['capabilities'].get('asyncDrain')):
        raise ValueError('Firebase consumer lacks required lifecycle ABI')
    return directory, contract


def dependency(contract):
    framework = contract['framework']
    return {'Name': framework['name'], 'Publisher': framework['publisher'],
            'MinVersion': framework['minVersion']}
