import sys, random
random.seed(1)
rows = ["id,first_name,last_name,age,city,balance,active"]
cities = ["Austin", "Denver", "Miami", "Seattle", "Boston", "Reno"]
fn = ["Ana", "Bob", "Cara", "Dan", "Eve", "Finn", "Gina", "Hugo"]
ln = ["Lee", "Ng", "Ortiz", "Park", "Quinn", "Rao", "Shaw", "Tan"]
for i in range(200):
    rows.append(f"{1000+i},{random.choice(fn)},{random.choice(ln)},"
                f"{random.randint(18,80)},{random.choice(cities)},"
                f"{random.randint(0,99999)/100:.2f},{random.choice(['true','false'])}")
open(sys.argv[1], "w").write("\n".join(rows) + "\n")
