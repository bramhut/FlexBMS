from pathlib import Path

Import("env")

partition_table = Path(env.subst("$PROJECT_DIR")) / "partitions.csv"
for line in partition_table.read_text(encoding="utf-8").splitlines():
    row = [field.strip() for field in line.split(",")]
    if len(row) >= 4 and row[0] == "ota_0":
        env.Replace(ESP32_APP_OFFSET=row[3])
        break
else:
    raise RuntimeError("partitions.csv does not define an ota_0 application partition")
