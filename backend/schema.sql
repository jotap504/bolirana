-- ==========================================
-- SCRIPT DE BASE DE DATOS PARA SUPABASE
-- Ejecutar este script en el SQL Editor de Supabase
-- ==========================================

-- 1. Tabla de Perfiles de Jugador (profiles)
-- Almacena los perfiles de los usuarios registrados via Google OAuth
create table if not exists public.profiles (
  id uuid references auth.users on delete cascade primary key,
  name text not null,
  avatar_url text,
  zone text,
  updated_at timestamp with time zone default timezone('utc'::text, now()) not null
);

-- Habilitar RLS para profiles
alter table public.profiles enable row level security;

-- Políticas de Seguridad RLS para profiles
create policy "Permitir lectura publica de perfiles" on public.profiles
  for select to public using (true);

create policy "Permitir a los usuarios modificar su propio perfil" on public.profiles
  for all to authenticated
  using ((select auth.uid()) = id)
  with check ((select auth.uid()) = id);


-- 2. Tabla de Rankings de Partidas (rankings)
-- Almacena el historial de puntajes cargados por la maquina o el celular del jugador
create table if not exists public.rankings (
  id bigint generated always as identity primary key,
  created_at timestamp with time zone default timezone('utc'::text, now()) not null,
  arcade_id text not null,
  player_name text not null,
  score integer not null,
  zone text,
  is_guest boolean default true,
  google_id uuid references auth.users(id) on delete set null
);

-- Habilitar RLS para rankings
alter table public.rankings enable row level security;

-- Políticas de Seguridad RLS para rankings
create policy "Permitir lectura publica de rankings" on public.rankings
  for select to public using (true);

create policy "Permitir insert publico de rankings" on public.rankings
  for insert to public with check (true);
