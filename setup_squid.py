import paramiko

def run():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        print("Connecting to Moscow server...")
        client.connect("147.45.219.76", port=22, username="root", password="M7VEVVGghY8XH", timeout=15)
        
        cmds = [
            "apt-get update",
            "apt-get install -y squid",
            "cat > /etc/squid/squid.conf << 'EOF'\n"
            "http_port 3128\n"
            "acl all src all\n"
            "http_access allow all\n"
            "EOF\n",
            "systemctl restart squid",
            "systemctl status squid | grep Active"
        ]
        
        for cmd in cmds:
            print(f"Running: {cmd}")
            stdin, stdout, stderr = client.exec_command(cmd)
            out = stdout.read().decode('utf-8')
            err = stderr.read().decode('utf-8')
            if out: print(out.strip())
            if err: print("ERR:", err.strip())
            
    except Exception as e:
        print("Error:", e)
    finally:
        client.close()

if __name__ == "__main__":
    run()
