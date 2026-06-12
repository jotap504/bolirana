import urllib.request
import json

def read_env(env_path):
    env = {}
    with open(env_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                key, val = line.split('=', 1)
                env[key.strip()] = val.strip()
    return env

def inspect():
    env_path = r"c:\Users\user\Documents\bolirana\backend\.env"
    env = read_env(env_path)
    url = env.get("SUPABASE_URL")
    anon_key = env.get("SUPABASE_ANON_KEY")
    
    # Try fetching rankings
    rankings_url = f"{url}/rest/v1/rankings?select=*"
    try:
        req = urllib.request.Request(
            rankings_url,
            headers={
                "apikey": anon_key,
                "Authorization": f"Bearer {anon_key}"
            }
        )
        with urllib.request.urlopen(req, timeout=5) as response:
            res = json.loads(response.read().decode('utf-8'))
            print(f"[OK] rankings exists. Rows: {len(res)}")
    except Exception as e:
        print("[ERROR] rankings table fetch error:", e)

if __name__ == "__main__":
    inspect()
