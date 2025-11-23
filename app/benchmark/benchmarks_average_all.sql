select operation,dataset,tool,avg(real_sec) as real_sec_avg, avg(user_sec) as user_sec_avg, avg(sys_sec) as sys_sec_avg, avg(max_rss_bytes) as max_rss_bytes_avg
from data
group by operation,dataset,tool
