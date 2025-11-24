select operation,dataset,tool,
min(real_sec) as real_sec_min, min(user_sec) as user_sec_min, min(sys_sec) as sys_sec_min, min(max_rss_bytes) as max_rss_bytes_min,
avg(real_sec) as real_sec_avg, avg(user_sec) as user_sec_avg, avg(sys_sec) as sys_sec_avg, avg(max_rss_bytes) as max_rss_bytes_avg
from data
group by operation,dataset,tool
order by case when tool = 'zsv' then '0' when tool = 'xsv' then '1' when tool like 'zsv%' then 'a' when tool like 'polars%' then 'b' else tool end , operation,dataset
