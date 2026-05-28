from datetime import datetime
from sqlalchemy import String, Integer, Float, Boolean, DateTime, Text, JSON
from sqlalchemy.orm import Mapped, mapped_column
from .database import Base

class ConfigEntry(Base):
    __tablename__ = "config"
    key:   Mapped[str] = mapped_column(String, primary_key=True)
    value: Mapped[str] = mapped_column(Text)

class GameSession(Base):
    __tablename__ = "game_sessions"
    id:           Mapped[int]      = mapped_column(Integer, primary_key=True, autoincrement=True)
    started_at:   Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    ended_at:     Mapped[datetime] = mapped_column(DateTime, nullable=True)
    player_count: Mapped[int]      = mapped_column(Integer)
    mode:         Mapped[str]      = mapped_column(String(20))
    scores:       Mapped[dict]     = mapped_column(JSON, default=list)
    credits_used: Mapped[int]      = mapped_column(Integer, default=0)

class Transaction(Base):
    __tablename__ = "transactions"
    id:         Mapped[int]      = mapped_column(Integer, primary_key=True, autoincrement=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    kind:       Mapped[str]      = mapped_column(String(20))
    amount:     Mapped[float]    = mapped_column(Float)
    credits:    Mapped[int]      = mapped_column(Integer)
    reference:  Mapped[str]      = mapped_column(String(100), nullable=True)

class Prize(Base):
    __tablename__ = "prizes"
    id:         Mapped[int]      = mapped_column(Integer, primary_key=True, autoincrement=True)
    kind:       Mapped[str]      = mapped_column(String(20))
    value:      Mapped[str]      = mapped_column(String(100))
    redeemed:   Mapped[bool]     = mapped_column(Boolean, default=False)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
