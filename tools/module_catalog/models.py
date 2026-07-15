import json
from typing import List, Dict, Any, Optional

class ModuleInfo:
    def __init__(
        self,
        tid: str,
        name: str,
        aliases: Optional[List[str]] = None,
        repository: str = "",
        description_en: str = "",
        description_source: str = "",
        tid_evidence: Optional[List[str]] = None,
        confidence: str = "unresolved",
        notes: str = ""
    ):
        self.tid = tid.strip().upper()
        self.name = name.strip()
        self.aliases = aliases or []
        self.repository = repository.strip()
        self.description_en = description_en.strip()
        self.description_source = description_source.strip()
        self.tid_evidence = tid_evidence or []
        self.confidence = confidence.strip()
        self.notes = notes.strip()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "aliases": sorted(list(set(self.aliases))),
            "repository": self.repository,
            "description_en": self.description_en,
            "description_source": self.description_source,
            "tid_evidence": sorted(list(set(self.tid_evidence))),
            "confidence": self.confidence,
            "notes": self.notes
        }

    @classmethod
    def from_dict(cls, tid: str, data: Dict[str, Any]) -> 'ModuleInfo':
        return cls(
            tid=tid,
            name=data.get("name", ""),
            aliases=data.get("aliases", []),
            repository=data.get("repository", ""),
            description_en=data.get("description_en", ""),
            description_source=data.get("description_source", ""),
            tid_evidence=data.get("tid_evidence", []),
            confidence=data.get("confidence", "unresolved"),
            notes=data.get("notes", "")
        )

    def __repr__(self) -> str:
        return f"<ModuleInfo {self.tid} - {self.name} ({self.confidence})>"
