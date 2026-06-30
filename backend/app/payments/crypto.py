import os
import base64
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.fernet import Fernet

PAYMENTS_DIR = os.path.dirname(__file__)
FERNET_KEY_FILE = os.path.join(PAYMENTS_DIR, '.fernet_key')
RSA_PRIV_FILE = os.path.join(PAYMENTS_DIR, '.rsa_private_key')

_fernet = None
_private_key = None

def get_fernet() -> Fernet:
    global _fernet
    if _fernet is not None:
        return _fernet
    if not os.path.exists(FERNET_KEY_FILE):
        key = Fernet.generate_key()
        with open(FERNET_KEY_FILE, 'wb') as f:
            f.write(key)
    else:
        with open(FERNET_KEY_FILE, 'rb') as f:
            key = f.read()
    _fernet = Fernet(key)
    return _fernet

def get_rsa_private_key() -> rsa.RSAPrivateKey:
    global _private_key
    if _private_key is not None:
        return _private_key
    if not os.path.exists(RSA_PRIV_FILE):
        private_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        )
        with open(RSA_PRIV_FILE, 'wb') as f:
            f.write(pem)
        _private_key = private_key
    else:
        with open(RSA_PRIV_FILE, 'rb') as f:
            pem = f.read()
        _private_key = serialization.load_pem_private_key(pem, password=None)
    return _private_key

def get_rsa_public_pem() -> str:
    priv = get_rsa_private_key()
    pub = priv.public_key()
    pem = pub.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    return pem.decode('utf-8')

def decrypt_rsa_payload(base64_ciphertext: str) -> str:
    try:
        ciphertext = base64.b64decode(base64_ciphertext)
        priv = get_rsa_private_key()
        decrypted = priv.decrypt(
            ciphertext,
            padding.OAEP(
                mgf=padding.MGF1(algorithm=hashes.SHA256()),
                algorithm=hashes.SHA256(),
                label=None
            )
        )
        return decrypted.decode('utf-8')
    except Exception as e:
        raise ValueError(f'Error al desencriptar carga RSA: {str(e)}')

def encrypt_data(data: str) -> str:
    if not data:
        return ''
    f = get_fernet()
    return f.encrypt(data.encode('utf-8')).decode('utf-8')

def decrypt_data(encrypted: str) -> str:
    if not encrypted:
        return ''
    try:
        f = get_fernet()
        return f.decrypt(encrypted.encode('utf-8')).decode('utf-8')
    except Exception:
        return encrypted
