class stats:

    def __init__(self, file_path) -> None:
        self.file = open(file_path)
        self.parse()

    def parse(self):
        details = []
        header = None
        detail = None

        # just parse the file first
        for str in self.file:
            if header is not None:
                detail = str.split()
                ht = dict()
                # expect that after each header
                # the stats line will be there
                assert(len(header)==len(detail))
                for i in range(0, len(header)):
                    ht[header[i]] = detail[i]
                details.append(ht)
                header = None
            else:
                header = str.split()
                if str.startswith("perf") or len(header)==0 or header[0] not in ["Total_nanoseconds", "syscall_timestamp"]:
                    header=None
        
        # now compute average stats
        total_stats = {}
        cnt = {}
        for stat in details:
            for key in stat.keys():
                total_stats.setdefault(key, 0)
                total_stats[key] += int(stat[key])
                cnt.setdefault(key, 0)
                cnt[key] += 1
        
        for key in total_stats.keys():
            total_stats[key] /= cnt[key]

        self.average_stats = total_stats

        # print(self.average_stats)

    def __del__(self):
        self.file.close()