-- ==========================================
-- SCRIPT DE BASE DE DATOS PARA SUPABASE (Actualizado con estadísticas y remera)
-- Ejecutar este script en el SQL Editor de Supabase
-- ==========================================

-- 1. Tabla de Perfiles de Jugador (profiles)
create table if not exists public.profiles (
  id uuid references auth.users on delete cascade primary key,
  name text not null,
  avatar_url text,
  zone text,
  updated_at timestamp with time zone default timezone('utc'::text, now()) not null
);

-- Habilitar RLS para profiles
alter table public.profiles enable row level security;

-- Limpiar políticas existentes para evitar errores al re-ejecutar
drop policy if exists "Permitir lectura publica de perfiles" on public.profiles;
drop policy if exists "Permitir a los usuarios modificar su propio perfil" on public.profiles;

-- Políticas de Seguridad RLS para profiles
create policy "Permitir lectura publica de perfiles" on public.profiles
  for select to public using (true);

create policy "Permitir a los usuarios modificar su propio perfil" on public.profiles
  for all to authenticated
  using ((select auth.uid()) = id)
  with check ((select auth.uid()) = id);

-- Columnas adicionales de personalización y estadísticas
alter table public.profiles
  add column if not exists age integer,
  add column if not exists club text,
  add column if not exists zone text,
  add column if not exists jersey_primary_color text default '#ffffff',
  add column if not exists jersey_secondary_color text default '#00ffcc',
  add column if not exists jersey_pattern text default 'plain',
  add column if not exists games_played integer default 0,
  add column if not exists high_score integer default 0,
  add column if not exists best_streak integer default 0,
  add column if not exists total_score integer default 0,
  add column if not exists updated_at timestamptz default now();

-- 3b. Storage: Políticas RLS del bucket 'avatars'
-- (Ejecutar después de crear el bucket 'avatars' como Public en el dashboard)
create policy if not exists "Public read avatars" on storage.objects
  for select to public using (bucket_id = 'avatars');

create policy if not exists "Auth users upload avatars" on storage.objects
  for insert to authenticated
  with check (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));

create policy if not exists "Auth users update own avatars" on storage.objects
  for update to authenticated
  using (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));

create policy if not exists "Auth users delete own avatars" on storage.objects
  for delete to authenticated
  using (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));


-- 2. Tabla de Rankings de Partidas (rankings)
create table if not exists public.rankings (
  id bigint generated always as identity primary key,
  created_at timestamp with time zone default timezone('utc'::text, now()) not null,
  arcade_id text not null,
  player_name text not null,
  score integer not null,
  zone text,
  is_guest boolean default true,
  google_id uuid references auth.users(id) on delete set null,
  avatar_url text
);

-- Habilitar RLS para rankings
alter table public.rankings enable row level security;

-- Limpiar políticas existentes para evitar errores al re-ejecutar
drop policy if exists "Permitir lectura publica de rankings" on public.rankings;
drop policy if exists "Permitir insert publico de rankings" on public.rankings;

-- Políticas de Seguridad RLS para rankings
create policy "Permitir lectura publica de rankings" on public.rankings
  for select to public using (true);

create policy "Permitir insert publico de rankings" on public.rankings
  for insert to public with check (true);

-- 3. Trigger de Postgres para actualizar estadísticas automáticamente
create or replace function public.update_profile_stats()
returns trigger as $$
begin
  if new.google_id is not null then
    update public.profiles
    set 
      games_played = games_played + 1,
      high_score = greatest(high_score, new.score),
      total_score = coalesce(total_score, 0) + new.score
    where id = new.google_id;
  end if;
  return new;
end;
$$ language plpgsql security definer;

-- Drop trigger si existe antes de crearlo
drop trigger if exists on_ranking_inserted on public.rankings;

create trigger on_ranking_inserted
  after insert on public.rankings
  for each row execute function public.update_profile_stats();

-- 4. Tabla de Máquinas (machines)
create table if not exists public.machines (
  arcade_id text primary key,
  name text not null,
  latitude double precision,
  longitude double precision,
  zone text,
  updated_at timestamp with time zone default timezone('utc'::text, now()) not null
);

-- Habilitar RLS para machines
alter table public.machines enable row level security;

drop policy if exists "Permitir lectura publica de maquinas" on public.machines;
drop policy if exists "Permitir insert/update publico de maquinas" on public.machines;

create policy "Permitir lectura publica de maquinas" on public.machines
  for select to public using (true);

create policy "Permitir insert/update publico de maquinas" on public.machines
  for all to public using (true) with check (true);

-- 5. Función de Postgres (RPC) para calcular el Ranking Zonal (Fórmula de Haversine en SQL puro)
create or replace function public.get_zonal_rankings(
  machine_lat double precision,
  machine_lon double precision,
  radius_km double precision
)
returns table (
  player_name text,
  score integer,
  zone text,
  avatar_url text,
  arcade_id text,
  distance double precision
) as $$
begin
  return query
  select 
    r.player_name,
    r.score,
    r.zone,
    coalesce(p.avatar_url, r.avatar_url) as avatar_url,
    r.arcade_id,
    (6371 * acos(
      cos(radians(machine_lat)) * cos(radians(m.latitude)) * 
      cos(radians(m.longitude) - radians(machine_lon)) + 
      sin(radians(machine_lat)) * sin(radians(m.latitude))
    )) as distance
  from public.rankings r
  join public.machines m on r.arcade_id = m.arcade_id
  left join public.profiles p on r.google_id = p.id
  where (6371 * acos(
    cos(radians(machine_lat)) * cos(radians(m.latitude)) * 
    cos(radians(m.longitude) - radians(machine_lon)) + 
    sin(radians(machine_lat)) * sin(radians(m.latitude))
  )) <= radius_km
  order by r.score desc
  limit 10;
end;
$$ language plpgsql security definer;

-- 6. Crear Vista de Efectividad (Rendimiento)
create or replace view public.effectiveness_ranking as
select 
  id,
  name,
  avatar_url,
  zone,
  games_played,
  (coalesce(total_score, 0)::double precision / nullif(games_played, 0)) as avg_score
from public.profiles
where games_played > 0;
