# PES-VCS: Version Control System

**Author:** Aadya Arvind  
**SRN:** PES2UG24CS008  
**Sem:** 4 **Sec:** A  

## Overview
A simplified Git-like version control system implemented in C.
Supports `init`, `add`, `status`, `commit`, and `log` commands.

## How to Build
```bash
sudo apt install -y gcc build-essential libssl-dev
make all
```

## How to Run
```bash
export PES_AUTHOR="Aadya Arvind <PES2UG24CS008>"
./pes init
./pes add <file>
./pes commit -m "message"
./pes log
```

## Phases
- **Phase 1:** Object store with SHA-256 hashing and directory sharding
- **Phase 2:** Tree objects for directory serialization  
- **Phase 3:** Index (staging area) implementation
- **Phase 4:** Commit creation and history traversal

## Report
See `PES2UG24CS008_OS_Orange-4_Report.pdf` for full screenshots and analysis.
