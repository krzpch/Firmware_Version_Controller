import hmac

def hmac_calc(data: bytes, key: bytes) -> bytes:
    return hmac.new(key,data,'sha256').digest()