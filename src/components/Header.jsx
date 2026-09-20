import React, { useState, useRef, useEffect } from 'react';
import { useNavigate, useLocation } from 'react-router-dom';
import './Header.css';
import AuthModal from './AuthModal';

export default function Header() {
  const [isProductsOpen, setIsProductsOpen] = useState(false);
  const [isAuthOpen, setIsAuthOpen] = useState(false);
  const [isMenuOpen, setIsMenuOpen] = useState(false);
  const [isScrolled, setIsScrolled] = useState(false);
  const dropdownRef = useRef(null);
  const navigate = useNavigate();
  const location = useLocation();

  useEffect(() => {
    function handleClickOutside(event) {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setIsProductsOpen(false);
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  useEffect(() => {
    const onScroll = () => setIsScrolled(window.scrollY > 24);
    onScroll();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);

  useEffect(() => {
    setIsMenuOpen(false);
    setIsProductsOpen(false);
  }, [location.pathname]);

  const products = [
    {
      id: 'external',
      title: 'EXTERNAL',
      status: 'Undetected',
      icon: <img src="/external.png" className="product-icon-img" alt="" />
    },
    {
      id: 'trigger',
      title: 'TRIGGER',
      status: 'Undetected',
      icon: <img src="/external.png" className="product-icon-img" alt="" />
    }
  ];

  const handleProductClick = (gameId) => {
    setIsProductsOpen(false);
    setIsMenuOpen(false);
    navigate(`/games/${gameId}`);
  };

  const handleLogin = () => {
    setIsAuthOpen(false);
    navigate('/home');
  };

  const handleDashboardClick = () => {
    const token = localStorage.getItem('token');
    if (token) {
      navigate('/home');
    } else {
      setIsAuthOpen(true);
    }
  };

  const handleLogout = () => {
    localStorage.removeItem('token');
    localStorage.removeItem('adminToken');
    navigate('/');
    window.location.reload();
  };

  const goFaq = () => {
    setIsMenuOpen(false);
    if (location.pathname === '/') {
      document.getElementById('faq')?.scrollIntoView({ behavior: 'smooth' });
    } else {
      navigate('/#faq');
    }
  };

  const token = localStorage.getItem('token');

  return (
    <header className={`site-nav ${isScrolled ? 'is-scrolled' : ''}`}>
      <div className="site-nav__inner">
        <button type="button" className="nav__brand" onClick={() => navigate('/')} aria-label="На главную">
          <img src="/logo.svg" className="logo-icon" alt="shadow" />
        </button>

        <nav className="nav__center" aria-label="Основное меню">
          <div className="nav__item nav__item--menu" ref={dropdownRef}>
            <button
              type="button"
              className={`nav__link ${isProductsOpen ? 'is-open' : ''}`}
              aria-expanded={isProductsOpen}
              aria-haspopup="true"
              onClick={() => setIsProductsOpen(!isProductsOpen)}
            >
              Товары
              <span className="nav__caret" aria-hidden="true" />
            </button>
            {isProductsOpen && (
              <div className="nav__dropdown products-dropdown">
                {products.map((p) => (
                  <button
                    type="button"
                    key={p.id}
                    className="product-dropdown-card"
                    onClick={() => handleProductClick(p.id)}
                  >
                    <span className="product-icon">{p.icon}</span>
                    <span className="product-name">{p.title}</span>
                    <span className="product-status">{p.status}</span>
                  </button>
                ))}
              </div>
            )}
          </div>
          <a href="#" className="nav__link">Новости</a>
          <button type="button" className="nav__link" onClick={goFaq}>FAQ</button>
        </nav>

        <div className="nav__right">
          <button type="button" className="cta-outline nav__cabinet" onClick={handleDashboardClick}>
            Кабинет
          </button>
          {token && (
            <button type="button" className="nav__link nav__logout" onClick={handleLogout}>
              Выйти
            </button>
          )}
          <button
            type="button"
            className="nav__burger"
            aria-expanded={isMenuOpen}
            aria-label={isMenuOpen ? 'Закрыть меню' : 'Открыть меню'}
            onClick={() => setIsMenuOpen(!isMenuOpen)}
          >
            <span />
            <span />
          </button>
        </div>
      </div>

      {isMenuOpen && (
        <div className="nav__sheet">
          <button type="button" className="nav__sheet-link" onClick={() => { setIsProductsOpen(true); }}>
            Товары
          </button>
          {isProductsOpen && products.map((p) => (
            <button type="button" key={p.id} className="nav__sheet-sub" onClick={() => handleProductClick(p.id)}>
              {p.title}
            </button>
          ))}
          <a href="#" className="nav__sheet-link">Новости</a>
          <button type="button" className="nav__sheet-link" onClick={goFaq}>FAQ</button>
          <button type="button" className="cta-fill nav__sheet-cta" onClick={handleDashboardClick}>
            Личный кабинет
          </button>
        </div>
      )}

      <AuthModal
        isOpen={isAuthOpen}
        onClose={() => setIsAuthOpen(false)}
        onLogin={handleLogin}
      />
    </header>
  );
}
