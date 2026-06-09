import logging
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession, async_sessionmaker
from sqlalchemy.orm import DeclarativeBase
from .config import DB_PATH

log = logging.getLogger(__name__)

db_url = f"sqlite+aiosqlite:///{DB_PATH}"
engine = create_async_engine(db_url, echo=False, connect_args={"timeout": 30})
AsyncSessionLocal = async_sessionmaker(engine, expire_on_commit=False)

class Base(DeclarativeBase):
    pass

async def init_db():
    from . import models  # Asegura el registro de los modelos en Base.metadata
    log.info("Inicializando base de datos SQLite en: %s", db_url)
    try:
        async with engine.begin() as conn:
            await conn.run_sync(Base.metadata.create_all)
        log.info("Base de datos SQLite inicializada correctamente.")
    except Exception as e:
        log.error("Fallo critico al inicializar base de datos SQLite: %s", e, exc_info=True)
        raise e

async def get_db():
    async with AsyncSessionLocal() as session:
        yield session
